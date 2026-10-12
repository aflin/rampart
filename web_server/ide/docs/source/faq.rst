Frequently Asked Questions
==========================

.. include:: special.rst

General
-------

How do I install Rampart?
~~~~~~~~~~~~~~~~~~~~~~~~~

The quickest way is the install script, which detects your platform,
downloads the matching build, verifies its checksum and runs the
installer:

.. code-block:: bash

    curl -fsSL https://get.rampart.dev/ | sh

It supports Linux x86_64 and aarch64 (glibc 2.17 or newer), 32-bit
Raspberry Pi and other armv7l systems (glibc 2.28 or newer, i.e. Debian 10
and up), macOS 11 (Big Sur) or newer on both Apple Silicon and Intel, and
FreeBSD 14 or newer.  On any other system it stops with a specific reason
rather than installing something that will not run.

Run without ``sudo``, it installs to ``~/.rampart``.  For a system-wide
install to ``/usr/local/rampart``, pipe to ``sudo sh`` instead:

.. code-block:: bash

    curl -fsSL https://get.rampart.dev/ | sudo sh

The files are still left owned by the user who invoked ``sudo``, not by
root, so ongoing maintenance — including ``rampart --install`` below —
does not need ``sudo`` afterwards.

To place the files yourself instead, download a ``.tar.gz`` for your
platform from
`rampart.dev/downloads/latest/ <https://rampart.dev/downloads/latest/>`_
and extract it:

.. code-block:: bash

    tar xzf rampart-<version>-<platform>.tar.gz
    cd rampart

From there, either run the bundled ``./install.sh``, or run in place —
Rampart needs no installation step so long as the extracted directory
structure is kept intact.  Run ``./bin/rampart`` directly, or add the
``bin/`` directory to your ``PATH``.

**Adding optional modules.**  The base install carries the core
modules.  Others — Python interop, langtools, tree-sitter, chromeview
and so on — are downloaded on demand:

.. code-block:: bash

    rampart --install --list                        # show what is available
    rampart --install rampart-python rampart-langtools  # install named modules
    rampart --install all                           # install everything available

Where a module has hardware-specific builds, each is its own name.
``rampart-langtools-cu11``, ``-cu12`` and ``-cu13`` are the CUDA builds,
and ``rampart-langtools-arm8a`` is a faster ARMv8-A build for a Pi 3 or
newer.  Installing one re-points ``rampart-llamacpp.so``,
``rampart-faiss.so`` and ``rampart-clip.so`` at its builds; switch back by
installing a different variant.

Note that ``--install`` works only on official builds — the ones the
install script and the download page provide.  A Rampart you compiled
yourself will refuse, since the distribution packages are not
necessarily ABI-compatible with it.


What is Rampart and how does it differ from Node.js?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rampart is a low-memory-footprint JavaScript runtime that uses the
`Duktape JavaScript Engine <https://duktape.org>`_ to bring together
high-performance C libraries for web and information management
applications.

While both Rampart and Node.js let you write server-side JavaScript,
they have fundamental differences:

+---------------------+-------------------------------------------+--------------------------------------------+
| Feature             | Rampart                                   | Node.js                                    |
+=====================+===========================================+============================================+
| JS Engine           | Duktape (compact, low memory)             | V8 (fast JIT compilation, higher memory)   |
+---------------------+-------------------------------------------+--------------------------------------------+
| Threading           | Native POSIX threads, each with its own   | Single-threaded event loop; worker_threads |
|                     | JS interpreter; threads are integral to   | available but not used by the core HTTP    |
|                     | the built-in HTTP server                  | server or standard libraries               |
+---------------------+-------------------------------------------+--------------------------------------------+
| Event Loop          | libevent2                                 | libuv                                      |
+---------------------+-------------------------------------------+--------------------------------------------+
| Included Modules    | SQL database, full-text search, HTTP      | HTTP, filesystem, streams, child processes |
|                     | server, crypto, HTML parser, Redis,       |                                            |
|                     | LMDB, Python interop, and more            |                                            |
+---------------------+-------------------------------------------+--------------------------------------------+
| Package Ecosystem   | Built-in modules; no npm                  | npm with hundreds of thousands of packages |
+---------------------+-------------------------------------------+--------------------------------------------+
| Performance Profile | Slower JS interpretation, but C-backed    | Fast JS execution, I/O performance depends |
|                     | functions provide speed where it counts   | on ecosystem packages                      |
+---------------------+-------------------------------------------+--------------------------------------------+
| Module Format       | CommonJS-style ``require()``              | CommonJS and ES Modules                    |
+---------------------+-------------------------------------------+--------------------------------------------+

Rampart's philosophy is that JavaScript orchestrates high-performance C
code.  The result is a single product that replaces an entire
`LAMP <https://en.wikipedia.org/wiki/LAMP_(software_bundle)>`_ or
`MEAN <https://en.wikipedia.org/wiki/MEAN_(solution_stack)>`_ stack while
consuming considerably fewer resources.


Can I use npm packages with Rampart?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Some of them.  The experimental
:ref:`rampart-nodeshim <rampart-extras:rampart-nodeshim Module>` module
provides node's core modules (``fs``, ``path``, ``stream``, ``events``
and others), which is enough to run many pure-JavaScript npm packages —
``axios``, ``cheerio``, ``commander``, ``markdown-it``, ``pino``,
``yaml`` and ``zod`` among those tested.

Three things to know before relying on it:

* **You install the packages with npm yourself.**  Rampart does not
  fetch dependencies; run ``npm install`` to produce a
  ``node_modules/`` tree and Rampart resolves from it by bare name.

* **It requires the transpiler.**  npm packages are almost always
  written in modern JavaScript, which Duktape's ES5 parser will not
  accept, so in practice you need the ``-t`` flag.

* **It is not fully compatible.**  There is no ESM, no compiled native
  add-ons (``.node`` binaries), and core-module coverage is partial.
  Whether a given package works is best-effort, so test before
  committing to one.

See the
:ref:`rampart-nodeshim <rampart-extras:rampart-nodeshim Module>` section
for the full list of submodules, the known gaps, and the tested
libraries.

That said, prefer Rampart's own modules where they cover the task —
they are written in C and are considerably faster than the equivalent
npm package running through the shim.  Most of what you would reach for
npm to provide is already in the distribution.

Some functionality is **built into the rampart executable** and is always
available without ``require()``:

* **Utility functions** — ``rampart.utils`` (file I/O, printf, exec, etc.)
* **Threading** — ``rampart.thread`` (POSIX threads, locks, clipboard)
* **Vector operations** — ``rampart.vector`` (typed vectors, distance metrics)
* **Events** — ``rampart.event`` (cross-thread event system)
* **CSV import** — ``rampart.import`` (CSV parsing)

The remaining functionality ships as **C modules** that come with the
distribution and are loaded with ``require()``:

* **HTTP server** — ``rampart-server`` (multi-threaded, WebSocket support)
* **HTTP client** — ``rampart-curl`` (HTTP, FTP, SMTP, POP3, IMAP)
* **SQL database** — ``rampart-sql`` (Texis with full-text search)
* **Key-value store** — ``rampart-lmdb``
* **Redis** — ``rampart-redis``
* **Crypto** — ``rampart-crypto`` (OpenSSL)
* **HTML parsing** — ``rampart-html`` (Tidy-HTML5)
* **Markdown** — ``rampart-cmark``
* **Image processing** — ``rampart-gm`` (GraphicsMagick)
* **Text extraction** — ``rampart-totext`` (PDF, DOCX, XLSX, etc.)
* **Python interop** — ``rampart-python``
* **Networking** — ``rampart-net`` (TCP sockets, SSL/TLS)
* **robots.txt** — ``rampart-robots``

You can also write your own C modules using the Duktape C API (see
`How do I write a C module for Rampart?`_).


What version of ECMAScript does Rampart support?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, Rampart supports partial ECMAScript 2015 (ES6) and
ECMAScript 2016 (ES7) through Duktape.  This includes ``TypedArray``,
``Buffer``, ``Proxy``, ``Symbol``, template literals, ``BigInt`` (with
``123n`` literals and ``BigInt64Array``), and ``WeakRef`` /
``WeakMap`` / ``FinalizationRegistry``.

For full ES2015+ support — including ``async``/``await``, destructuring,
arrow functions, ``for...of``, ``class`` syntax, and Promises — place one
of the following directives at the top of your script:

.. code-block:: javascript

    "use transpiler"; // ES2015+ via a fast, built-in C transpiler
                      //    OR
    "use babel";      // Full ES2015+ via the Babel transpiler (much slower)

The built-in transpiler (``"use transpiler"``) is written in C, on top of
tree-sitter, and is significantly faster than Babel, which runs as
JavaScript.  In both
cases, the transpiled output is cached to disk (e.g.,
``myscript.transpiled.js`` or ``myscript.babel.js``).  On subsequent
runs, the cached version is reused if the source file has not changed,
so the startup cost is only paid once.


What are Rampart's non-standard JavaScript extensions?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rampart adds two extensions to template literal syntax.  These work
with ``"use transpiler"`` or without any transpiler, but are **not**
available with ``"use babel"``:

**Template literal sprintf shortcut** — embed format specifiers directly
in template literals:

.. code-block:: javascript

    var name = "<b>world</b>";
    console.log(`Hello ${%H:name}`);
    // equivalent to: console.log("Hello " + sprintf("%H", name));
    // output: Hello &lt;b&gt;world&lt;&#47;b&gt;

**Triple-backtick unescaped strings** — backslashes are treated as
literal characters:

.. code-block:: javascript

    var path = ```C:\Users\myfile.txt```;
    // path === "C:\\Users\\myfile.txt"

These are Rampart-specific extensions and are not part of the ECMAScript
standard.  They work with ``"use transpiler"`` but not with
``"use babel"``.


Are operations synchronous or asynchronous by default?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Synchronous by default.** Unlike Node.js, where I/O functions
typically return Promises or accept callbacks, most Rampart functions
block until they complete:

.. code-block:: javascript

    // These all block:
    var data = rampart.utils.readFile("/path/to/file");
    var res  = curl.fetch("http://example.com/");
    var rows = sql.exec("SELECT * FROM bigtable", {maxRows: -1});
    var val  = redis.get("mykey");

This makes Rampart scripts straightforward to write — no callback
pyramids or ``await`` chains for simple operations.

When you need non-blocking behavior, opt in explicitly:

* ``curl.fetchAsync()`` / ``curl.submitAsync()`` — async HTTP requests
* Redis commands with ``{async: true}`` — async Redis operations
* ``rampart.thread`` — offload work to a background thread
* ``setTimeout()`` / ``setInterval()`` — deferred execution

This is a significant difference from Node.js, where the default is
asynchronous and you opt in to synchronous variants (e.g.,
``fs.readFileSync()``).


What is rampart.globalize and why do I see it in so many scripts?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Many Rampart scripts begin with:

.. code-block:: javascript

    rampart.globalize(rampart.utils);

This copies all properties of ``rampart.utils`` into the global scope,
so you can call utility functions directly by name.  The difference is
significant — compare:

.. code-block:: javascript

    // Without globalize:
    rampart.utils.fprintf(rampart.utils.stderr, "Error: %s\n", msg);
    var data = rampart.utils.readFile("/path/to/file");
    var info = rampart.utils.stat("/path/to/file");
    rampart.utils.printf("count: %d\n", n);

    // With globalize:
    rampart.globalize(rampart.utils);

    fprintf(stderr, "Error: %s\n", msg);
    var data = readFile("/path/to/file");
    var info = stat("/path/to/file");
    printf("count: %d\n", n);

For C programmers, the globalized form is immediately familiar —
``fprintf(stderr, ...)``, ``printf()``, ``stat()``, ``fopen()``,
``fclose()``, ``sleep()`` all work just as you would expect.

You can also globalize selectively if you only want a few functions:

.. code-block:: javascript

    rampart.globalize(rampart.utils, ["printf", "sprintf", "fprintf", "stderr"]);

If you prefer not to pollute the global namespace, use
``rampart.localize()`` inside a function to make the same names available
as local variables:

.. code-block:: javascript

    function handleRequest(req) {
        rampart.localize(rampart.utils);
        // printf, sprintf, etc. available here but not globally
        printf("handling request\n");
    }


Modules
-------

How does require() work compared to Node.js?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rampart uses a CommonJS-style ``require()`` function, but the module
search path is different from Node.js.  There is no ``node_modules``
directory.

The extension is optional.  For a bare name, each directory on the search
path is tried with ``.js``, then ``.json``, then ``.so``, then the name
exactly as given.  Naming the extension (``require("foo.so")``) selects
that file.  A path ending in ``/`` loads ``index.js`` from that directory.

``require()`` searches in this order:

1. The absolute path (if one is given), and nothing else.
2. When running a :ref:`single-file bundle <rampart-extras:single-file bundles>`,
   the appended zip, before any location on disk.
3. The calling module's own directory (if called from within a module).
4. ``process.scriptPath``, then its ``modules/`` and
   ``lib/rampart_modules/`` subdirectories.
5. ``~/.rampart/modules/`` and ``~/.rampart/lib/rampart_modules/``.
6. The ``$RAMPART_PATH`` environment variable (if set).
7. ``process.installPath``, then its ``modules/`` and
   ``lib/rampart_modules/`` subdirectories — the last of which is
   ``process.modulesPath``.

C modules (``.so`` shared libraries) are searched the same way.

.. code-block:: javascript

    var Sql  = require("rampart-sql");       // included C module
    var conf = require("./myconf.json");     // JSON is parsed and returned
    var util = require("./myutil");          // extension optional

**The current working directory is not searched.**  A relative id such as
``require("./myutil")`` resolves against the script or module doing the
requiring, not against the directory you happen to be standing in — so a
script behaves the same no matter where it is run from.

Modules are loaded once and cached — subsequent ``require()`` calls for the
same module return the cached instance.


What is the difference between require() and rampart.include()?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``require()`` loads a module with its own scope.  The module exports
values via ``module.exports``, and its internal variables are private.

``rampart.include()`` executes a JavaScript file in the **global** scope —
all variables and functions defined in the included file become global.
It also automatically processes Babel if ``"use babel"`` is specified in
the included file.

.. code-block:: javascript

    // module pattern (private scope)
    var mymod = require("mymodule");
    mymod.doSomething();

    // include pattern (global scope)
    rampart.include("helpers.js");
    helperFunction();  // now available as a global

Use ``require()`` for encapsulated, reusable code.  Use
``rampart.include()`` when you intentionally want to merge code into the
global namespace.


How do I write a C module for Rampart?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you are a C developer, you can extend Rampart with native modules
using the Duktape C API.

Include the ``rampart.h`` header and export a ``duk_open_module``
function:

.. code-block:: c

    #include "rampart.h"

    static duk_ret_t timesthree(duk_context *ctx) {
        double val = duk_get_number(ctx, 0);
        duk_push_number(ctx, val * 3);
        return 1;
    }

    duk_ret_t duk_open_module(duk_context *ctx) {
        duk_push_c_function(ctx, timesthree, 1);
        return 1;
    }

Compile as a shared library:

.. code-block:: bash

    # Linux
    cc -I/usr/local/rampart/include -fPIC -shared \
       -Wl,-soname,times3.so -o times3.so times3.c

    # macOS
    cc -I/usr/local/rampart/include -dynamiclib \
       -undefined dynamic_lookup \
       -install_name times3.so -o times3.so times3.c

Then load it like any other module:

.. code-block:: javascript

    var x3 = require("times3");
    console.log(x3(7));  // 21

For something small, you can skip the separate build step entirely.
:ref:`rampart-cmodule <rampart-extras:rampart-cmodule>` compiles C
source at run time and hands back a callable JavaScript function:

.. code-block:: javascript

    var cmodule = require('rampart-cmodule.js');

    var timesThree = cmodule("timesThree", `
    {
        double val = REQUIRE_NUMBER(ctx, 0, "timesThree: argument 1 must be a Number");
        duk_push_number(ctx, val * 3);
        return 1;
    }`);

    console.log(timesThree(7));  // 21

You supply only ``#include`` lines and the function body — no signature.
The generated ``.c`` and ``.so`` are written to the current directory,
so a later ``cmodule("timesThree")`` loads the built module instead of
recompiling.  A C compiler must be present on the machine.  See
:ref:`rampart-cmodule <rampart-extras:rampart-cmodule>` for support
functions, compiler flags and libraries.

The full Duktape C API documentation is available at
`duktape.org/api.html <https://duktape.org/api.html>`_.


Concurrency and Asynchronous Programming
-----------------------------------------

How does Rampart's threading model work?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rampart provides **real POSIX threads**, each with its own independent
Duktape JavaScript interpreter and event loop.  Threading in Rampart has
two distinct but related uses:

**General-purpose threads** — created with ``new rampart.thread()`` in
any script, with or without the HTTP server.  These are useful for
CPU-bound work, background tasks, parallel processing, or any situation
where you want concurrent execution:

.. code-block:: javascript

    var thr = new rampart.thread();

    thr.exec({
        threadFunc: function(arg) {
            // runs in a separate OS thread with its own JS interpreter
            return arg * 2;
        },
        threadArg: 21,
        callbackFunc: function(result, error) {
            // runs back in the main thread's event loop
            console.log(result);  // 42
        }
    });

All threads stay alive between calls and can be reused.  Normally, when
a thread's event loop is empty and the parent thread is ready to exit,
the thread closes automatically.  Passing ``true`` creates a
**persistent** thread that prevents this auto-close, keeping the thread
alive even when idle — it must be explicitly closed with ``thr.close()``:

.. code-block:: javascript

    var thr = new rampart.thread(true);  // persistent — won't auto-close

    thr.exec(doWork, firstJob, onDone);
    // later...
    thr.exec(doWork, secondJob, onDone);

    // must explicitly close, or the parent will wait forever
    thr.close();

**Server threads** — ``rampart-server`` internally creates a pool of
these same threads (default: one per CPU core) and automatically
dispatches incoming HTTP and WebSocket requests across them.  You do not
create these yourself; they are managed by ``server.start()``.

Both types share the same underlying mechanism and the same rules apply
to each.

Node.js offers ``worker_threads`` with separate V8 isolates, which is a
similar concept.  The difference is one of integration: Rampart's
``rampart.thread`` is a first-class primitive used throughout the
platform — including by the built-in HTTP server — whereas Node's
``worker_threads`` are an opt-in addition that the standard HTTP server
and most libraries do not use.

**Key points:**

* Each thread has a completely separate JavaScript context — there is
  no shared memory between threads.
* Global variables defined **before** ``new rampart.thread()`` are copied
  into the new thread's context.  Variables defined **after** the thread
  is created are **not** available.
* Use ``rampart.thread.put()`` / ``rampart.thread.get()`` (the thread
  clipboard) or ``rampart.event`` for inter-thread communication.
* Use ``new rampart.lock()`` for mutual exclusion when coordinating
  access to external resources.
* Threads can be created inside other threads, but a child thread can
  only be used from the thread that created it.


Why can't my thread see variables I defined after creating it?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Only global variables that exist at the moment ``new rampart.thread()``
is called are copied into the thread's JavaScript context.  Variables
created afterward are invisible to the thread.

.. code-block:: javascript

    var available = "I exist before the thread";
    var thr = new rampart.thread();
    var notAvailable = "I was defined too late";

    thr.exec(function() {
        console.log(available);     // works
        console.log(notAvailable);  // undefined!
    });

This is because each thread receives a **snapshot** of the global scope
at creation time.  If you need to pass data to a running thread later,
use the thread clipboard:

.. code-block:: javascript

    rampart.thread.put("myKey", someValue);

    // inside the thread:
    var val = rampart.thread.get("myKey");


How do threads communicate with each other?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are three mechanisms affecting communication between threads:

**Thread Clipboard** — a key-value store shared across all threads:

.. code-block:: javascript

    // Thread A
    rampart.thread.put("result", {count: 42});

    // Thread B
    var data = rampart.thread.get("result");  // deep copy

``rampart.thread.get()`` returns a deep copy.  You can also use
``rampart.thread.waitfor("key", timeout)`` to block until a key is
available, and ``rampart.thread.onGet("key*", callback)`` to be
notified when matching keys are set.

**Events** — broadcast notifications across threads:

.. code-block:: javascript

    // Listening thread
    rampart.event.on("myevent", "handler1", function(uservar, triggerval) {
        console.log("got:", triggerval);
    });

    // Triggering thread
    rampart.event.trigger("myevent", "hello");

**Locks** — POSIX mutexes for critical sections:

.. code-block:: javascript

    var lock = new rampart.lock("mylock");
    lock.lock();
    // critical section
    lock.unlock();


Does Rampart support Promises and async/await?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes, but you must enable a transpiler by placing one of the following at
the top of your script:

.. code-block:: javascript

    "use transpiler"; // ES2015+ via a fast, built-in C transpiler (recommended)
                      //     OR
    "use babel";      // Full ES2015+ via the Babel transpiler (much slower)

Example:

.. code-block:: javascript

    "use transpiler";

    function fetchData() {
        return new Promise(resolve => {
            setTimeout(() => resolve("done"), 100);
        });
    }

    async function main() {
        var result = await fetchData();
        console.log(result);  // "done"
    }

    main();

The ``rampart-curl`` and ``rampart-redis`` modules are designed to take
advantage of this.  Their async variants — ``curl.fetchAsync()``,
``curl.submitAsync()``, and Redis async commands — return Promises and
work with ``async``/``await`` when a transpiler is enabled:

.. code-block:: javascript

    "use transpiler";

    var curl = require("rampart-curl");

    async function main() {
        var res = await curl.fetchAsync("http://example.com/");
        console.log(res.status);  // 200
    }

    main();

Without a transpiler, you can still use ``setTimeout``,
``setInterval``, and callback-based patterns for asynchronous work.


What are the transpiler gotchas I should know about?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When using ``"use transpiler"``, be aware of these limitations:

* ``const`` is transpiled to ``var`` — reassignment silently succeeds
  rather than throwing.

``await`` in a loop does pause per iteration, and destructuring an
awaited value works; both were limitations in earlier releases.

``"use babel"`` handles more edge cases correctly but is much slower.
Test your specific patterns if in doubt.


What happens to my threads when the script exits?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``process.exit()`` is immediate: threads still running are terminated
where they are, and work in progress is lost.  A thread that is part way
through writing a file will not finish.

Letting the script end normally is the orderly path.  A non-persistent
thread closes once its event loop is empty and the parent is ready to
exit; a persistent thread (``new rampart.thread(true)``) keeps the
process alive until you call ``thr.close()``.  If you have state to
flush, flush it before exiting rather than relying on the thread to get
there first.

Note also that ``fork()`` and ``daemon()`` refuse to run while threads
are open — see `How do fork() and daemon() work?`_.


Why did my setTimeout callback fire late?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rampart's event loop is based on libevent2.  Synchronous, blocking calls
prevent the event loop from running, which delays all pending callbacks:

.. code-block:: javascript

    setTimeout(function() {
        console.log("hello");  // you might expect this after 100ms
    }, 100);

    rampart.utils.sleep(3);  // blocks the event loop for 3 seconds!
    // "hello" prints AFTER the 3-second sleep, not after 100ms

The rule: avoid long-running synchronous operations if you depend on
timely callback execution.  Use threads for CPU-bound work, and use the
async variants of ``rampart-curl`` and ``rampart-redis`` for I/O-bound
work.


HTTP Server
-----------

How does rampart-server compare to Express.js?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``rampart-server`` is a multi-threaded HTTP/HTTPS server module based on
libevhtp_ws and libevent2.  It ships with the Rampart distribution and is
loaded with ``require("rampart-server")``.  Unlike Express, it is not a
framework you install separately.

Key differences:

* **Threading** — ``rampart-server`` automatically dispatches requests
  across a thread pool (default: one thread per CPU core), each with its
  own JavaScript context and event loop.  Express runs on Node's
  single-threaded event loop by default.
* **Routing** — Routes are defined in a ``map`` object.  Priority is
  determined by path specificity (exact > regex > glob), not declaration
  order.  Express uses middleware chain order.
* **No middleware chain** — Instead of ``app.use()``, Rampart provides
  ``beginFunc`` (runs before each request), ``endFunc`` (after), and
  ``notFoundFunc`` hooks.
* **Module hot-reload** — Mapped modules are re-checked on each request
  and reloaded if changed on disk.  No server restart required.
* **WebSockets included** — No external package needed; part of
  ``rampart-server``.
* **Reverse proxy** — A map entry may be ``{proxy: "http://backend:3000/"}``
  instead of a directory or function, WebSocket upgrades included.  No
  separate ``http-proxy-middleware``.
* **Rate limiting** — ``rateLimit`` takes per-path rules keyed by ``ip``,
  ``fingerprint`` (IP plus browser headers) or ``cookie:name``.  All
  matching prefixes are checked, so a global limit and a tighter one on
  ``/apps/auth/`` compose.
* **Several listeners in one process** — ``listen:[]`` takes a list of
  bind blocks, so http and https, different certificates and different
  routing can share a process.

.. code-block:: javascript

    var server = require("rampart-server");

    server.start({
        bind: "0.0.0.0:8080",
        map: {
            "/":            "/var/www/html",        // static files
            "/api/search":  function(req) {         // dynamic route
                                return {json: {results: []}};
                            },
            "ws:/chat":     function(req) {         // websocket
                                req.wsSend("hello");
                            }
        }
    });


How do I add authentication and sessions?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``rampart-auth`` provides session-based authentication for
``rampart-server``.  The session check runs in C — cookie extraction,
LMDB session lookup, expiry and privilege level — so it costs no
JavaScript on a request that is merely authenticated.

Turn it on with two properties in ``web_server_conf.js`` (or in
``server.start()``):

.. code-block:: javascript

    var serverConf = {
        authMod:     true,
        authModConf: working_directory + '/auth-conf.js'
    };

``auth-conf.js`` is where protected paths, privilege levels, session
lifetime and lockout policy live.  The companion ``auth.js`` in
``web_server/apps/`` handles the operations that do not need to be fast:
account creation, login and logout, password management, an
administrative web interface and a CLI tool.  CSRF protection, sliding
expiry, account lockout, email-based password reset, and Google and
Facebook OAuth plugins are included.  Setting ``authMod`` to a
:green:`Function` replaces the whole mechanism with your own — useful for
API-key or header-based schemes.

See :ref:`rampart-auth <rampart-auth:The rampart-auth module>`.


How do I structure a Rampart web application?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

With the standard server layout, app modules live in ``apps/`` and are
automatically mapped to URLs.  A module can export a single handler
function or an :green:`Object` mapping paths to different handlers:

.. code-block:: javascript

    // apps/myapp.js — multiple endpoints from one file
    module.exports = {
        "/":              index_page,
        "/index.html":    index_page,
        "/search.json":   search_handler,
        "/autocomplete":  autocomplete_handler
    };

    function index_page(req) {
        return {html: "<h1>My App</h1>"};
    }

    function search_handler(req) {
        var q = req.query.q || "";
        // ... search logic ...
        return {json: {results: results}};
    }

A common pattern is to make scripts **dual-mode** — they serve as both
web handlers and CLI setup tools:

.. code-block:: javascript

    rampart.globalize(rampart.utils);
    var Sql = require("rampart-sql");
    var db_path = serverConf ? serverConf.dataRoot + "/mydb"
                             : process.scriptPath + "/../data/mydb";

    if(module && module.exports) {
        // Web server mode — export handlers
        module.exports = {
            "/":            index_page,
            "/search.json": search
        };
    } else {
        // CLI mode — build the database
        var sql = new Sql.connection(db_path, true);
        sql.exec("CREATE TABLE ...");
        // ... import data ...
    }

Run ``rampart apps/myapp.js`` to set up the database, then start the
server and the same script handles requests.


What is web_server_conf.js and when should I use it?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

While the ``rampart-server`` module gives you full flexibility to build a
server from scratch, the Rampart distribution includes a ready-to-use
web server layout in the ``web_server/`` directory of the installation or
distribution directory, with a standard structure:

.. code-block:: none

    web_server/
        web_server_conf.js   — main configuration script
        html/                — static files (document root)
        apps/                — server-side JavaScript app modules
        wsapps/              — WebSocket app modules
        data/                — application data
        logs/                — access and error logs

The ``web_server_conf.js`` script is a self-contained configuration file
that you edit to set up your server.  It wraps the
``rampart-webserver`` module (a JavaScript module included with the
distribution) and provides everything most applications need out of the
box:

* **Start/stop/restart/status** commands — run it as
  ``rampart web_server_conf.js start|stop|restart|status``
* **HTTP to HTTPS redirection** — set ``redir: true`` or
  ``redirPort: 80``
* **Let's Encrypt integration** — set ``letsencrypt: "yourdomain.com"``
  for automatic HTTPS setup
* **Self-signed certificates** — set ``selfSign: true`` for development
* **Process monitoring** — set ``monitor: true`` to auto-restart on crash
* **Log rotation** — set ``rotateLogs: true`` with configurable interval
  and retention
* **Daemon mode** — set ``daemon: true`` to detach from the terminal
* **Privilege dropping** — set, e.g. ``user: "www-data"`` when binding to
  ports < 1024 as root

To get started, copy the ``web_server`` directory to your project and
edit ``web_server_conf.js``.  The configuration is a single
:green:`Object` with commented-out defaults — uncomment and change what
you need:

.. code-block:: javascript

    var serverConf = {
        bindAll:       true,
        port:          443,
        secure:        true,
        letsencrypt:   "example.com",
        redir:         true,          // redirect HTTP port 80 -> HTTPS
        user:          "www-data",
        daemon:        true,
        monitor:       true,
        rotateLogs:    true,
        rotateInterval: "daily",
        serverRoot:    working_directory
    };

    require("rampart-webserver").web_server_conf(serverConf);

Place static files in ``html/``, app modules in ``apps/``, and WebSocket
modules in ``wsapps/``.  The URL mapping is automatic — for example, a
module at ``apps/search.js`` is served at ``/apps/search/`` or
``/apps/search.html``.  You can also
add custom mappings via the ``appendMap`` option without replacing the
default layout.

For most use cases, editing ``web_server_conf.js`` is all you need.  The
lower-level ``rampart-server`` module is there when you need full control
over routing, custom request handling, or non-standard server
architectures.


How do I serve static files?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The easiest way is to use the standard server layout (see
`What is web_server_conf.js and when should I use it?`_).  Place your
files in the ``html/`` directory and they are served automatically at the
root URL.

If you need custom headers for a specific path — for example,
cache-control headers for an images directory — use the ``appendMap``
option in ``web_server_conf.js``:

.. code-block:: javascript

    var serverConf = {
        // ... other settings ...
        appendMap: {
            "/images": {
                path: working_directory + "/html/images",
                headers: {"Cache-Control": "max-age=31536000, public"}
            }
        },
        serverRoot: working_directory
    };

If you are using the ``rampart-server`` module directly, map a URL path
to a filesystem directory in the ``map`` object:

.. code-block:: javascript

    server.start({
        map: {
            "/":       "/path/to/html",
            "/images": {
                path: "/path/to/images",
                headers: {"Cache-Control": "max-age=31536000, public"}
            }
        }
    });

In both cases, the server automatically serves ``index.html`` for
directory requests, maps file extensions to MIME types, supports gzip
compression, and handles HTTP Range requests for partial content.


How do I handle POST data and file uploads?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

POST data is available on the ``req`` object passed to your route
function:

* ``req.body`` — the raw request body as a :green:`Buffer`.
* ``req.postData`` — parsed form data (``application/x-www-form-urlencoded``)
  or JSON (``application/json``) as an :green:`Object`.
* ``req.formData`` — multipart file uploads
  (``multipart/form-data``) as an :green:`Array` of objects containing
  file name, content type, and data.
* ``req.params`` — all parsed variables from postData, formData and/or the query string.  

.. code-block:: javascript

    function handlePost(req) {
        // URL-encoded or JSON POST
        var username = req.postData.content.username;

        // File upload
        if (req.formData && req.formData.content.length) {
            var file = req.formData.content[0];
            // file.filename, file["content-type"], file.content (Buffer)
        }

        return {json: {status: "ok"}};
    }

The maximum body size defaults to 50 MB and can be changed with the
``maxBodySize`` option in ``server.start()``.


How do I access request parameters?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are several sources of request data on the ``req`` object:

.. code-block:: javascript

    function handler(req) {
        // URL query string: /search?q=hello&page=2
        var q    = req.query.q || "";
        var page = parseInt(req.query.page) || 1;

        // req.params merges query + POST + cookies — use for flexibility
        // (accepts both GET and POST for the same endpoint)
        var q = req.params.q || "";

        // Parsed POST body (form data or JSON)
        var username = req.postData.content.username;

        // Raw body as Buffer
        var raw = req.body;

        // HTTP method, headers, cookies, client IP
        req.method;    // "GET", "POST", etc.
        req.headers;   // request headers object
        req.cookies;   // parsed cookies object
        req.ip;        // client IP address
    }

Use ``req.query`` when you specifically need GET parameters.  Use
``req.params`` when you want an endpoint to accept both GET and POST.


How do WebSockets work?
~~~~~~~~~~~~~~~~~~~~~~~

With the standard server layout, place a WebSocket module in the
``wsapps/`` directory and it is automatically mapped — for example,
``wsapps/chat.js`` is served at ``ws://wsapps/chat``.

When using ``rampart-server`` directly, prefix a path with
``"ws:"`` in the map:

.. code-block:: javascript

    server.start({
        map: {
            "ws:/chat": function(req) {
                if (req.count == 0) {
                    // First call: client just connected, no websocket
                    // upgrade yet
                    do_setup();

                    // set a disconnect callback
                    req.wsOnDisconnect(function() {
                        // cleanup on disconnect
                    });
                } else {
                    // Subsequent calls: client sent data
                    var message = rampart.utils.bufferToString(req.body);
                    req.wsSend("Echo: " + message);
                }
            }
        }
    });

Key details:

* The callback is invoked once on connection (``req.count == 0``), then
  again each time the client sends data.
* The ``req`` object persists across calls for the same connection — you
  can attach custom properties to it.
* ``req.wsSend(data)`` sends data to the client (:green:`String` for
  text, :green:`Buffer` for binary, :green:`Object` for JSON).
* ``req.wsEnd()`` closes the connection.
* Use ``rampart.event`` to broadcast messages to all connected clients
  across threads.


How do I return different content types?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Mapped functions return an :green:`Object` with a key that indicates the
MIME type:

.. code-block:: javascript

    // HTML
    return {html: "<h1>Hello</h1>"};

    // JSON
    return {json: {status: "ok", count: 42}};

    // Plain text
    return {txt: "Hello, world"};

    // Binary data
    return {bin: myBuffer, headers: {"Content-Type": "application/octet-stream"}};

    // Serve a file directly (efficient — no readFile needed)
    return {html: "@/var/www/page.html"};

    // Custom status code and headers
    return {
        status: 302,
        headers: {"Location": "/new-page"}
    };

Available shorthand keys include ``html``, ``json``, ``txt``, ``xml``,
``css``, ``js``, ``bin``, ``pdf``, ``png``, ``jpg``, ``gif``, and
others.  You can also set arbitrary ``Content-Type`` values via the
``headers`` property.


How do I handle async operations in a mapped function?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Mapped functions normally return a response synchronously.  When you
need to do something asynchronous — fetch data from an external API,
wait on a timer, or run work in a thread — return ``{defer: true}``
and call ``req.reply()`` later.  This frees the server thread to handle
other requests while waiting.

.. code-block:: javascript

    var curl = require("rampart-curl");

    function handler(req) {
        // Start an async fetch — doesn't block the server thread
        curl.fetchAsync("https://api.example.com/data", function(res) {
            var data = JSON.parse(res.text);
            req.reply({json: {results: data}});
        });

        return {defer: true};  // don't send anything yet
    }

This also works with ``setTimeout`` and thread callbacks:

.. code-block:: javascript

    function slow_handler(req) {
        setTimeout(function() {
            req.reply({txt: "done waiting"});
        }, 2000);

        return {defer: true};
    }

Without ``defer``, the response is sent as soon as the function returns,
and the connection is closed before any async callback can reply.


How do I generate HTML in mapped functions?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The most common pattern uses template literals with Rampart's sprintf
format codes for safe output:

.. code-block:: javascript

    function search_page(req) {
        var query = req.query.q || "";
        var results = doSearch(query);
        return {html: `
    <!DOCTYPE html>
    <html><body>
      <h3>Results for: ${%H:query}</h3>
      <pre>${%3J:results}</pre>
    </body></html>
        `};
    }

``${%H:variable}`` HTML-escapes the value (prevents XSS).  ``${%3J:var}``
pretty-prints as JSON with 3-space indent.  These work with
``"use transpiler"`` or without any transpiler (not with babel).

For large responses, build content incrementally using the server buffer
instead of string concatenation:

.. code-block:: javascript

    function large_page(req) {
        req.put('<!DOCTYPE html><html><body>');
        for (var i = 0; i < rows.length; i++) {
            req.printf('<div class="item">%H</div>', rows[i].title);
        }
        return {html: '</body></html>'};
    }


How do I make HTTP requests from Rampart?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use ``rampart-curl``:

.. code-block:: javascript

    var curl = require("rampart-curl");

    // Simple GET
    var res = curl.fetch("https://api.example.com/data");
    if (res.status === 200) {
        var data = JSON.parse(res.text);
    }

    // POST with form data
    var res = curl.fetch("https://api.example.com/submit",
        {post: "name=value&other=data"});

    // POST with JSON
    var res = curl.fetch("https://api.example.com/submit",
        {postJSON: {name: "value"}});

    // With options
    var res = curl.fetch(url, {
        maxTime: 10,                     // timeout in seconds
        headers: "Authorization: Bearer TOKEN"
    });

``curl.fetch()`` is synchronous and returns an :green:`Object` with
``status``, ``text`` (response body as string), ``body`` (as buffer),
and ``headers``.  For async HTTP requests, use ``curl.fetchAsync()``
with a transpiler — see the ``rampart-curl`` documentation.


Why don't my variables persist between requests?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each server thread has its own isolated JavaScript context.  Module-level
variables are **not shared** across threads, and each thread loads its
own copy of your modules.

.. code-block:: javascript

    // This will NOT work as a shared counter:
    var count = 0;
    function handler(req) {
        count++;  // each thread has its own copy of count
        return {json: {count: count}};
    }

For shared state, use an external store:

* ``rampart-sql`` or ``rampart-lmdb`` for persistent shared data.
* ``rampart-redis`` for in-memory shared state.
* ``rampart-lmdb`` for on-disk shared state.
* The thread clipboard (``rampart.thread.put()`` /
  ``rampart.thread.get()``) for simple cross-thread values.


Database and Search
-------------------

What is Texis and how does it compare to SQLite?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``rampart-sql`` embeds **Texis**, a full SQL relational database engine
developed by Thunderstone, LLC.  It includes **Metamorph**, an
integrated full-text search engine with linguistic capabilities far
beyond SQLite's FTS.

+---------------------+--------------------------------------------+-----------------------------------+
| Feature             | Texis (rampart-sql)                        | SQLite                            |
+=====================+============================================+===================================+
| Full-Text Search    | Metamorph: suffix/prefix processing,       | FTS5: basic tokenized search,     |
|                     | thesaurus, phrase proximity, wildcards,    | limited ranking                   |
|                     | relevance ranking, linguistic derivations  |                                   |
+---------------------+--------------------------------------------+-----------------------------------+
| Vector Search       | ``varvec*`` columns, ``CREATE VECTOR``     | Requires extensions               |
|                     | ``INDEX`` (FAISS/IVFPQ or usearch/HNSW),   |                                   |
|                     | ``LIKEV`` search, and ``embed()`` /        |                                   |
|                     | ``chunkembed()`` running the model         |                                   |
|                     | inside the engine; keyword+vector          |                                   |
|                     | fusion in one statement                    |                                   |
+---------------------+--------------------------------------------+-----------------------------------+
| Geocoding           | Built-in ``latlon2geocode()`` for          | Requires extensions               |
|                     | geographic bounded-area searches           |                                   |
+---------------------+--------------------------------------------+-----------------------------------+
| Data Types          | VARCHAR, INT, DOUBLE, DATE, COUNTER,       | TEXT, INTEGER, REAL, BLOB, NULL   |
|                     | STRLST, VARBYTE, INDIRECT, GEOCODE,        |                                   |
|                     | VARVECF32/F16/BF16/I8/U8/B8                |                                   |
+---------------------+--------------------------------------------+-----------------------------------+
| Storage             | Directory-based (one dir per database)     | Single file                       |
+---------------------+--------------------------------------------+-----------------------------------+

.. code-block:: javascript

    var Sql = require("rampart-sql");
    var sql = new Sql.connection("/path/to/mydb", true);  // true = create

    sql.exec("CREATE TABLE docs (title VARCHAR(128), body VARCHAR(8000))");
    sql.exec("INSERT INTO docs VALUES(?, ?)", ["My Title", "The full text..."]);
    // the default word expression ([\uword]{2,99}) matches whole
    // utf-8 characters in any script; no options needed
    sql.exec("CREATE FULLTEXT INDEX docs_body_ftx ON docs(body)");

    var results = sql.exec(
        "SELECT title FROM docs WHERE body LIKEP 'full text search'",
        {maxRows: 100}
    );


How does full-text index maintenance work?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Full-text search with ``LIKEP`` always works, even on newly inserted
rows.  Texis will do a linear scan of any rows not yet covered by the
index, so queries always return correct results.

However, full-text indexes are **not automatically updated** on a
schedule unless you explicitly configure one.  As the number of
unindexed rows grows, the linear scan portion becomes slower.  For large
or frequently updated tables, you should set up index maintenance.

**Manual update** — rebuild or optimize the index when convenient:

.. code-block:: javascript

    // Rebuild the index
    sql.exec("CREATE FULLTEXT INDEX docs_body_ftx ON docs(body)");

    // Or optimize only if enough rows have changed
    sql.exec("ALTER INDEX docs_body_ftx OPTIMIZE HAVING COUNT(NewRows) > 1000");

**Scheduled update** — use ``scheduleUpdate()`` to have Rampart
automatically check and update the index at a regular interval:

.. code-block:: javascript

    // Check every 2 hours starting now; optimize if >= 1000 rows changed
    sql.scheduleUpdate("docs_body_ftx", "now", "2 hours", 1000);

The schedule is stored in the database's ``SYSUPDATE`` table, and a
monitor process is launched automatically to perform the updates.  See
the *Text Index Maintenance* section and the ``scheduleUpdate()``
function in the ``rampart-sql`` documentation for full details.

Regular B-tree indexes (``CREATE INDEX``) are maintained automatically on
every INSERT, UPDATE, and DELETE — this applies only to full-text
indexes.


How do I build a full-text search over documents (PDF, DOCX, etc.)?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use ``rampart-totext`` to extract plain text from documents, then store
and index the text with ``rampart-sql``.

``rampart-totext`` handles a wide range of formats out of the box —
DOCX, PPTX, XLSX, ODT, PDF, HTML, Markdown, RTF, LaTeX, man pages,
EPUB, and more.  Most formats are processed entirely in C with no
external dependencies.  PDF requires the ``pdftotext`` utility
(``poppler-utils`` package), and legacy ``.doc`` requires ``catdoc``.

Here is a complete example that imports a directory of documents into a
searchable database:

.. code-block:: javascript

    rampart.globalize(rampart.utils);
    var Sql    = require("rampart-sql");
    var totext = require("rampart-totext");

    var sql = new Sql.connection("/path/to/searchdb", true);

    sql.exec(
        "CREATE TABLE docs (filename VARCHAR(256), body VARCHAR(64000))"
    );

    // Import documents from a directory
    var files = readdir("/path/to/documents");
    for (var i = 0; i < files.length; i++) {
        var filepath = "/path/to/documents/" + files[i];
        try {
            var text = totext.convertFile(filepath);
            sql.exec("INSERT INTO docs VALUES(?, ?)", [files[i], text]);
        } catch(e) {
            printf("Skipping %s: %s\n", files[i], e.message);
        }
    }

    // Create a full-text index
    sql.exec("CREATE FULLTEXT INDEX docs_body_ftx ON docs(body)");

    // Search
    var results = sql.exec(
        "SELECT filename FROM docs WHERE body LIKEP ?",
        ["search terms here"],
        {maxRows: 20}
    );

You can also use ``totext.convert()`` to extract text from in-memory
content — for example, from a file uploaded via the HTTP server:

.. code-block:: javascript

    function handleUpload(req) {
        var totext = require("rampart-totext");

        if (req.formData && req.formData.content.length) {
            var file = req.formData.content[0];
            var text = totext.convert(file.content);
            // index 'text' into your database
        }
        return {json: {status: "ok"}};
    }

For details on ``convertFile()`` and ``convert()`` options, including
MIME type detection via the ``details`` parameter, see the
``rampart-totext`` documentation.


How do I do semantic (vector) search?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Embedding happens **inside** the SQL engine.  Load a model once on the
connection; ``embed()`` and the ``LIKEV`` operator then use it
automatically.

.. code-block:: javascript

    sql.set({llamaEmbed: "/path/to/model.gguf"});

    sql.exec("CREATE TABLE docs (title varchar(128), doc varchar(8000), v varvecF32(384))");
    sql.exec("INSERT INTO docs VALUES(?,?,embed(?))", [title, body, body]);

    // LIKEV takes a vector, or a String which is embedded with the same model
    var hits = sql.exec("SELECT title, $rank FROM docs WHERE v LIKEV ?",
                        ["what I am looking for"]);

Rows come back already ordered by ``$rank``, best first — no ``ORDER BY``
needed.  For large tables add a vector index (``CREATE VECTOR INDEX``);
without one a ``LIKEV`` is refused rather than scanned, and the only
sign is a soft error in ``sql.errMsg``.

Three engines are available, one per connection: ``llamaEmbed``
(llama.cpp GGUF), ``onnxEmbed`` (an ONNX model directory) and
``clipEmbed`` (CLIP image/text models).  See
:ref:`Vector Search <rampart-sql:Vector Search>` in the rampart-sql
documentation — in particular :ref:`rampart-sql:Vector Indexes` and
:ref:`Querying with LIKEV <querying-with-likev>` — and the knobs in
:ref:`sql-set <sql-set:llamaEmbed>`.


Which embedding engine should I use?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use ``llamaEmbed`` unless you have a reason not to.  It is the more
mature path and is GPU-accelerated on every supported platform — Metal
on Apple Silicon, CUDA on Linux.

``onnxEmbed`` accelerates only through CUDA, so on macOS it always runs
on the CPU.  It is the right choice when you already have an ONNX model,
or need ONNX-only features.

``clipEmbed`` is for CLIP models, where images and text share one vector
space — ``embed(?, 'image')`` takes an image path, so you can search
images with a text query.

Model files can be fetched by short name rather than path with
``rampart-models``: ``models.get("bge-m3:q8_0")``.  See
:ref:`rampart-langtools <rampart-langtools:The rampart-models module>`.


How do I search images with a text query?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Load a CLIP model instead of a text embedding model.  CLIP puts images
and text in one vector space, so a row whose vector came from a
photograph can be found by a sentence describing it:

.. code-block:: javascript

    sql.set({clipEmbed: models.get("clip-vit-b-32-laion:q8_0")});

    sql.exec("create table pics (Path varchar(256), Vec varvecF16)");

    /* 'image' tells embed() the parameter is an image path */
    sql.exec("insert into pics values (?, embed(?, 'image'))",
             [path, path]);

    var hits = sql.exec("select Path, $rank from pics where Vec likev ?",
                        ["a dog playing on a beach"], {maxRows: 10});

Only image paths and text go over the wire to the embedding engine, and
``chunkembed()`` does not apply to CLIP models.  Outside SQL, the same
model is available directly through ``rampart-clip``.


How do I combine keyword and semantic search?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Put both in one ``WHERE`` with ``OR``.  Texis fuses the two ranked lists
with Reciprocal Rank Fusion, which usually beats either retriever alone:

.. code-block:: javascript

    var res = sql.exec(
        "SELECT id, title, $rank, $krank, $vrank FROM docs " +
        "WHERE doc LIKEP ? OR v LIKEV ?",
        [terms, terms], {maxRows: 20});

``$rank`` is the fused score (a positional value, not a calibrated
relevance).  ``$krank`` and ``$vrank`` expose what each side scored on
its own, and are ``0`` when that side did not match — useful for seeing
which retriever found a row.  They are meaningful in the ``SELECT`` list
only.

Pool sizes are set with ``likepRows`` and ``likevRows``.  For documents
too long for a single vector, ``chunkembed()`` stores every chunk's
vector in one column, so a match on any chunk finds the row.

See :ref:`Hybrid keyword + vector queries (rank fusion)
<hybrid-rank-fusion>` in the rampart-sql documentation.


When should I use rampart-sql vs rampart-lmdb vs rampart-redis?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each serves a different purpose:

* **rampart-sql** (Texis) — Use when you need relational queries, SQL
  joins, full-text search, or structured data with complex query
  patterns.  Best for content-heavy applications.

* **rampart-lmdb** — Use when you need a fast, embedded key-value store
  with ACID transactions.  Ideal for configuration data, caches,
  session stores, or any workload that maps naturally to key-value
  lookups.  Very high read performance.

* **rampart-redis** — Use when you need a shared in-memory data
  structure server.  Best for pub/sub messaging, real-time counters,
  queues, and scenarios where multiple processes or machines need to
  share state.  Requires a running Redis server.


What is sql.one() and when should I use it?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``sql.one()`` is a shortcut that returns a single row as an
:green:`Object`, or ``undefined`` if no rows match.  It is the most
common query pattern in real Rampart applications:

.. code-block:: javascript

    // Single row lookup
    var row = sql.one("SELECT * FROM users WHERE id = ?", [userId]);
    if (row) {
        printf("Found: %s\n", row.name);
    }

    // Check if a table exists
    if (!sql.one("SELECT * FROM SYSTABLES WHERE NAME = 'users'")) {
        sql.exec("CREATE TABLE users ...");
    }

Use ``sql.one()`` when you expect zero or one result.  Use
``sql.exec()`` when you need multiple rows or a callback for row-by-row
processing:

.. code-block:: javascript

    // Row-by-row callback — return false to stop early
    sql.exec("SELECT * FROM docs WHERE body LIKEP ?",
        {maxRows: 100},
        [searchTerms],
        function(row, i) {
            // process each row; i is 0-based index
            if (i >= 20) return false;  // stop after 20
        }
    );


What is the difference between sql.exec() and sql.query()?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Both execute SQL statements, but they differ in error handling:

* ``sql.exec()`` **throws on errors that prevent the statement from
  executing** — for example, a syntax error or a missing table.  However,
  it does **not** throw for all error conditions.  For instance, inserting
  a duplicate row into a table with a unique index will not throw; the
  insert silently fails (``rowCount`` is 0) and the error is reported in
  ``sql.errMsg``.

* ``sql.query()`` **never throws**.  Instead, it sets ``sql.errMsg``
  and returns normally.  Use this when you want full control over error
  handling.

For both functions, ``sql.errMsg`` is always set to the most recent
error or message.  Check it after any call where you need to know
whether something went wrong:

.. code-block:: javascript

    // exec() — throws on hard errors, but not on duplicate insert
    sql.exec("INSERT INTO users VALUES(?)", [name]);
    if (sql.errMsg) {
        // e.g., unique constraint violation — no throw, but errMsg is set
        console.log("Insert issue: " + sql.errMsg);
    }

    // query() — never throws, always check errMsg
    sql.query("INSERT INTO users VALUES(?)", [name]);
    if (sql.errMsg) {
        console.log("Insert issue: " + sql.errMsg);
    }

    // exec() WILL throw for hard errors like bad syntax:
    try {
        sql.exec("SELET * FORM users");  // typo
    } catch(e) {
        console.log("SQL error: " + e.message);
    }

Also be aware that SELECT statements default to returning a maximum of
**10 rows**.  Set ``maxRows`` to get more, or ``-1`` for unlimited:

.. code-block:: javascript

    var results = sql.exec("SELECT * FROM bigtable", {maxRows: 1000});
    var all     = sql.exec("SELECT * FROM bigtable", {maxRows: -1});


How do I prevent SQL injection?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Always use parameterized queries.  Pass values as an :green:`Array` or
:green:`Object` — never concatenate user input into SQL strings:

.. code-block:: javascript

    // SAFE — parameterized
    sql.exec("SELECT * FROM users WHERE name = ?", [userInput]);

    // SAFE — named parameters
    sql.exec("SELECT * FROM users WHERE name = ?name", {name: userInput});

    // DANGEROUS — string concatenation
    sql.exec("SELECT * FROM users WHERE name = '" + userInput + "'");

Note that even a single parameter must be wrapped in an :green:`Array`:
``[value]``, not ``value``.


File I/O and System
-------------------

How does file I/O work compared to Node's fs module?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rampart's file I/O lives in ``rampart.utils`` and follows a style closer
to C ``stdio`` than Node's ``fs`` module:

.. code-block:: javascript

    rampart.globalize(rampart.utils);

    // Read an entire file (returns a Buffer by default)
    var buf = readFile("/path/to/file");

    // Read as a string
    var str = readFile("/path/to/file", 0, 0, true);

    // Write to a file
    fprintf("/path/to/output.txt", "Hello %s\n", "world");

    // C-style file handle operations
    var fh = fopen("/path/to/file", "r");
    var line = fgets(fh, 4096);  // read up to 4096 bytes, stopping at newline
    fclose(fh);

    // Read lines with an iterator pattern
    var rl = readLine("/path/to/file");
    var line;
    while ((line = rl.next()) !== null) {
        // process line
    }

    // Get file metadata (like C stat())
    var info = stat("/path/to/file");
    // info.size, info.permissions, info.mtime, info.isFile, etc.

There are no streams or ``fs.createReadStream()`` equivalents.  For
large files, use ``readFile()`` with ``offset`` and ``length``
parameters, or ``readLine()`` for line-by-line reading of text.


What are the extended printf format codes?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rampart's ``printf``/``sprintf`` support all standard C format
specifiers plus several useful extensions:

+--------+-----------------------------+-----------------------------------------------+
| Code   | Purpose                     | Example                                       |
+========+=============================+===============================================+
| ``%s`` | String                      | ``sprintf("Hello %s", "world")``              |
+--------+-----------------------------+-----------------------------------------------+
| ``%d`` | Integer                     | ``sprintf("Count: %d", 42)``                  |
+--------+-----------------------------+-----------------------------------------------+
| ``%f`` | Float                       | ``sprintf("Pi: %.2f", 3.14159)``              |
+--------+-----------------------------+-----------------------------------------------+
| ``%J`` | JSON                        | ``sprintf("%2J", obj)`` — pretty-printed JSON |
+--------+-----------------------------+-----------------------------------------------+
| ``%H`` | HTML encode                 | ``sprintf("%H", "<script>")``                 |
|        |                             | → ``&lt;script&gt;``                          |
+--------+-----------------------------+-----------------------------------------------+
| ``%!H``| HTML decode                 | ``sprintf("%!H", "&amp;")`` → ``&``           |
+--------+-----------------------------+-----------------------------------------------+
| ``%U`` | URL encode                  | ``sprintf("%U", "a b")`` → ``a+b``            |
+--------+-----------------------------+-----------------------------------------------+
| ``%!U``| URL decode                  | ``sprintf("%!U", "a+b")`` → ``a b``           |
|        |                             | (also decodes ``a%20b``)                      |
+--------+-----------------------------+-----------------------------------------------+
| ``%B`` | Base64 encode               | ``sprintf("%B", data)``                       |
+--------+-----------------------------+-----------------------------------------------+
| ``%!B``| Base64 decode               | ``sprintf("%!B", b64str)``                    |
+--------+-----------------------------+-----------------------------------------------+
| ``%P`` | Pretty-print with wrapping  | ``sprintf("%0.60P", longText)`` — the         |
|        |                             | *precision* is the wrap width; a leading      |
|        |                             | number is the indent                          |
+--------+-----------------------------+-----------------------------------------------+

These are available in ``printf()``, ``sprintf()``, ``fprintf()``, and
``bprintf()`` (which returns a :green:`Buffer`).

.. code-block:: javascript

    rampart.globalize(rampart.utils);

    // Pretty-print an object as JSON with 3-space indent
    printf("%3J\n", {name: "Rampart", version: 1});

    // HTML-encode user input for safe output
    var safe = sprintf("%H", userInput);

    // Base64-encode binary data
    var encoded = sprintf("%B", binaryBuffer);


How do I run external commands?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use ``rampart.utils.exec()`` for fine-grained control, or
``rampart.utils.shell()`` for quick shell commands:

.. code-block:: javascript

    rampart.globalize(rampart.utils);

    // exec() — run a command with arguments
    var res = exec("ls", "-la", "/tmp");
    // res.stdout, res.stderr, res.exitStatus

    // With options
    var res = exec("mycommand", {
        timeout: 5000,           // kill after 5 seconds
        stdin: "input data",     // pipe data to stdin
        background: true,        // don't wait for completion
        env: {PATH: "/usr/bin"}, // set environment variables
        cd: "/working/dir"       // change directory before executing
    }, "arg1", "arg2");

    // shell() — run a command string in $SHELL (or bash, or sh)
    var res = shell("cat /etc/hostname | tr -d '\\n'");

Both return an :green:`Object` with ``stdout``, ``stderr``,
``exitStatus``, and ``timedOut`` properties.


How do fork() and daemon() work?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Both are real POSIX operations available via ``rampart.utils``.  They
have the same interface: return the child's PID to the parent and ``0``
to the child.  ``daemon()`` double-forks and detaches from the terminal.

.. code-block:: javascript

    rampart.globalize(rampart.utils);

    var pid = fork();   // or daemon() for background process
    if (pid > 0) {
        // parent — pid is the child's PID
    } else if (pid == 0) {
        // child (or daemon process)
    }

**Note:** ``fork()`` and ``daemon()`` cannot be used while threads are
open.  For IPC between forked processes, use ``newPipe()`` — see the
``rampart-utils`` documentation for details.  For daemonizing the HTTP
server, use ``server.start({daemon: true})`` or ``web_server_conf.js``
instead.


Buffers and Data
----------------

What buffer types are available and how do they differ?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rampart (via Duktape) provides several buffer types.  They share the
same underlying memory model but have different APIs and capabilities:

**Plain buffers** — lightweight, no property table, created with
``Uint8Array.allocPlain()``.  These are what
``rampart.utils.stringToBuffer()`` and ``rampart.utils.bprintf`` return.

**ArrayBuffer** — the standard ES2015 backing store.  Cannot be indexed
directly; use a typed array or DataView to access data.

**TypedArrays** — ``Uint8Array``, ``Int8Array``, ``Uint16Array``,
``Int16Array``, ``Uint32Array``, ``Int32Array``, ``Uint8ClampedArray``,
``Float32Array``, ``Float64Array``.  Provide indexed access with a
specific element type.  Multiple typed arrays can share the same
``ArrayBuffer``.

**DataView** — provides explicit get/set methods with endianness
control (``getInt16()``, ``setFloat32()``, etc.).

**Node.js Buffer** — Duktape's compatibility implementation.  Provides
``slice()``, ``copy()``, ``fill()``, ``equals()``, ``compare()``,
``concat()``, and typed read/write methods (``readUInt32LE()``,
``writeFloatBE()``, etc.).

The following table summarizes what works in Rampart:

+----------------------------+-------+--------+-----------+----------+--------+
| Feature                    | Plain | Array  | Typed     | DataView | Node.js|
|                            | Buf   | Buffer | Array     |          | Buffer |
+============================+=======+========+===========+==========+========+
| Index read/write           | Yes   | No     | Yes       | No       | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| ``.length``                | Yes   | No     | Yes       | No       | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| ``.byteLength``            | Yes   | Yes    | Yes       | Yes      | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| ``.byteOffset``            | Yes   | No     | Yes       | Yes      | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| ``.buffer`` (ArrayBuffer)  | Yes   | N/A    | Yes       | Yes      | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| ``.subarray()``            | Yes   | No     | Yes       | No       | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| ``.set()``                 | Yes   | No     | Yes       | No       | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| ``.slice()``               | Yes   | Yes    | Yes       | No       | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| ``.copy()``                | No    | No     | No        | No       | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| ``.fill()``                | Yes   | No     | Yes       | No       | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| ``.concat()``              | No    | No     | No        | No       | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| ``.equals()`` / compare    | No    | No     | No        | No       | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| ``.toString()`` / encoding | No    | No     | No        | No       | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| ``.toJSON()``              | No    | No     | No        | No       | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| read/writeUInt16LE, etc.   | No    | No     | No        | No       | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| getInt32/setFloat64, etc.  | No    | No     | No        | Yes      | No     |
+----------------------------+-------+--------+-----------+----------+--------+
| Endianness control         | No    | No     | No        | Yes      | Yes    |
+----------------------------+-------+--------+-----------+----------+--------+
| forEach/map/filter/sort/   | Yes   | No     | Yes       | No       | Yes    |
| reduce/indexOf/keys/values |       |        |           |          |        |
+----------------------------+-------+--------+-----------+----------+--------+

**Important notes for Node.js developers:**

* ``Buffer.from()`` accepts a :green:`String`, :green:`Buffer`,
  :green:`ArrayBuffer`, typed array, or :green:`Array` of numbers
  (e.g., ``Buffer.from([0x68, 0x65, 0x6c, 0x6c, 0x6f])``).  Values
  are truncated to 0–255.
* ``Buffer.alloc(n, fill)`` accepts a :green:`Number`, :green:`String`,
  or :green:`Buffer` as the fill value (e.g., ``Buffer.alloc(4, 0xFF)``).
* ``buf.toString(encoding)`` accepts ``utf8``, ``hex``, ``base64``,
  ``base64url``, ``latin1``/``binary``, ``utf16le``/``ucs2``, and
  ``ascii``. ``Buffer.from(string, encoding)``,
  ``Buffer.byteLength(string, encoding)``, and
  ``buf.write(string[, offset][, length][, encoding])`` accept the
  same set. ``rampart.utils.hexify()`` / ``sprintf("%B", buf)`` are
  still available and remain the right choice when working with
  plain (non-Buffer) byte buffers.
* ``buf.indexOf()``, ``buf.lastIndexOf()``, ``buf.includes()``,
  ``buf.swap16()``, ``buf.swap32()``, ``buf.swap64()``, and
  ``Buffer.isEncoding()`` are available. ``buf.keys()`` /
  ``buf.values()`` / ``buf.entries()`` return iterators, as in Node.
* ``buf.readBigInt64BE()`` / ``writeBigInt64BE()`` and the other
  ``BigInt`` accessors are **not** available.  This is a gap in the
  Buffer methods only: ``BigInt`` itself *is* implemented, so
  ``BigInt`` values may be used freely in JavaScript.  Read a 64-bit
  value with the 32-bit accessors and combine.
* Plain buffers, TypedArrays and node ``Buffer``\ s all have the usual
  higher-order methods — ``forEach``, ``map``, ``filter``, ``reduce``,
  ``sort``, ``indexOf`` and friends all work.  Note that ``map``,
  ``filter`` and ``slice`` return another buffer of the same type, not
  an :green:`Array`.

**Concatenating buffers:** Use ``bprintf('%s%s', buf1, buf2)`` — works
with any buffer type and returns a plain buffer.

**Printf format codes and buffers:** ``%s``, ``%B``/``%!B``,
``%U``/``%!U``, and ``%H``/``%!H`` all accept any buffer type as well
as strings.

**Practical guidance:** Most Rampart code uses plain buffers (from
``readFile()``, ``stringToBuffer()``) and Node.js Buffers (from
``Buffer.from()``).  Use ``bufferToString()`` / ``stringToBuffer()``
to convert.  Use ``hexify()`` / ``dehexify()`` for hex encoding,
``sprintf("%B", data)`` / ``sprintf("%!B", b64)`` for base64.


How do I handle objects with cyclic references?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

JavaScript's ``JSON.stringify()`` throws an error on objects that
reference themselves.  Rampart provides a round-trip solution:

**Serialize** with ``sprintf("%!J", obj)`` — cyclic references are
replaced with ``{"_cyclic_ref": "$"}`` markers instead of throwing:

.. code-block:: javascript

    rampart.globalize(rampart.utils);

    var obj = {name: "test", count: 42};
    obj.self = obj;  // cyclic reference

    var json = sprintf("%!J", obj);
    // '{"name":"test", "count":42, "self":{"_cyclic_ref": "$"}}'

**Restore** with ``JSON.parse(json, true)`` — passing ``true`` as the
second argument tells Rampart to resolve the ``_cyclic_ref`` markers
back into actual object references:

.. code-block:: javascript

    var restored = JSON.parse(json, true);

    restored.self === restored;              // true
    restored.self.self.self.name;            // "test"

The ``true`` second argument to ``JSON.parse()`` is a Rampart extension
— in standard JavaScript, the second argument is a reviver function.
Note that ``%J`` (without ``!``) will also fall back to this safe mode
automatically if it detects that normal serialization would fail due to
cyclic references.


Deployment
----------

How do I daemonize and deploy the Rampart server?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Using rampart --server** — the ``rampart`` executable can start a
server directly from the command line using the standard directory
layout.  It daemonizes by default, serves from the current directory
(or a specified root), and accepts options on the command line:

.. code-block:: bash

    # Start a server from the current directory
    rampart --server

    # Specify a root directory and port
    rampart --server --port 8080 --bindAll true /path/to/web_server

    # HTTPS with Let's Encrypt
    rampart --server --letsencrypt example.com --redir true

    # Stop it
    rampart --server --stop

    # Quick test server (foreground, single-threaded, directory listings)
    rampart --quickserver --port 3000

Run ``rampart --server --help`` for usage, ``--lsopts`` for all
available options, and ``--showdefaults`` to see the default
configuration.  ``--quickserver`` uses different defaults suited for
development: it runs in the foreground, uses a single thread, enables
directory listings, and disables logging.

**Using web_server_conf.js** — for deployments where you want a
persistent, version-controlled configuration, copy the ``web_server/``
directory from the distribution and edit ``web_server_conf.js`` (see
`What is web_server_conf.js and when should I use it?`_).  It provides
``start``, ``stop``, ``restart``, and ``status`` commands, log rotation,
and process monitoring:

.. code-block:: bash

    rampart web_server_conf.js start
    rampart web_server_conf.js stop
    rampart web_server_conf.js status

**Using the server module directly** — for full control over routing
and request handling, use ``rampart-server`` in your own script.  The
``daemon`` option (defaults to ``false``) detaches from the terminal:

.. code-block:: javascript

    var server = require("rampart-server");

    server.start({
        bind: "0.0.0.0:8080",
        daemon: true,
        user: "www-data",       // drop privileges
        map: { /* ... */ }
    });


How do I ship my application as a single file?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Append a zip to a copy of the ``rampart`` executable.  The result is one
file — scripts, native modules, HTML, everything — that runs on a machine
with nothing installed on it:

.. code-block:: bash

    cd mybundle && zip -qr ../payload.zip . && cd ..
    cp /path/to/rampart myapp && cat payload.zip >> myapp && chmod +x myapp

Nothing in the code has to change.  A virtual ``:zip:/`` namespace makes
every file-reading API resolve inside the bundle, so ``require()``,
``readFile()`` and the web server's static-file handler all work as they
did on disk, and ``process.scriptPath`` becomes ``:zip:``.  At the zip
root, a file named ``entry_script.js`` is run automatically.

The two directories that cannot live in a bundle are the ones that must
be writable — a database directory and a log directory.  Compute those at
runtime; ``rampart.utils.payloadGet`` is defined only in a bundle, which
makes a convenient test.

See :ref:`Single-File Bundles <rampart-extras:single-file bundles>`.


How do I set up HTTPS?
~~~~~~~~~~~~~~~~~~~~~~

Pass SSL options to ``server.start()``:

.. code-block:: javascript

    var server = require("rampart-server");

    server.start({
        bind: "0.0.0.0:443",
        secure: true,
        sslKeyFile:  "/etc/letsencrypt/live/example.com/privkey.pem",
        sslCertFile: "/etc/letsencrypt/live/example.com/fullchain.pem",
        sslMinVersion: "tls1.2",  // default
        map: { /* ... */ }
    });

When ``secure`` is ``true``, all bound addresses use HTTPS.  The
``rampart-webserver.js`` extras module also provides Let's Encrypt
integration for automated certificate management.


Interoperability
----------------

Can I call Python code from Rampart?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes.  The ``rampart-python`` module embeds a Python interpreter and
provides automatic type conversion between JavaScript and Python:

.. code-block:: javascript

    var python = require("rampart-python");

    // Import a Python module
    var math = python.import("math");
    var result = math.sqrt(16).toValue();   // 4

    // Run arbitrary Python code
    var mymod = python.importString(`
    def greet(name):
        return f"Hello, {name}!"
    `);

    var msg = mymod.greet("Rampart").toValue();
    console.log(msg);  // "Hello, Rampart!"

Type mapping:

+------------------------+------------------------+
| JavaScript             | Python                 |
+========================+========================+
| Number                 | float                  |
+------------------------+------------------------+
| String                 | str                    |
+------------------------+------------------------+
| Array                  | tuple                  |
+------------------------+------------------------+
| Object                 | dict                   |
+------------------------+------------------------+
| Buffer                 | bytes                  |
+------------------------+------------------------+
| Date                   | datetime               |
+------------------------+------------------------+

Note: Due to the Python GIL, Python code running in separate Rampart
threads will execute in separate processes rather than true threads.

The Rampart distribution also includes ``python3r`` and ``pip3r`` in its
``bin/`` directory.  These are standalone Python and pip executables that
share the same ``site-packages`` and library paths as the embedded Python
in ``rampart-python``.  Use ``pip3r`` to install packages that will be
available to both ``python3r`` and ``rampart-python``:

.. code-block:: bash

    pip3r install requests

Packages installed this way can then be imported from within Rampart:

.. code-block:: javascript

    var python = require("rampart-python");
    var requests = python.import("requests");


Can I use browser APIs like fetch, URL or Blob?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes, for a substantial subset.  ``fetch``, ``URL``, ``Headers`` /
``Request`` / ``Response`` / ``FormData``, ``Blob`` / ``File``, the
stream family, ``WebSocket``, ``XMLHttpRequest``, ``crypto`` (Web
Crypto), ``structuredClone`` and ``localStorage`` are all present.

They come from ``rampart --install rampart-whatwg rampart-intl``, which
the base install does not include.  Once installed there is nothing to
``require()``: the globals are built in and **lazy-loaded**, so nothing is
initialized until a script first references one of the names and scripts
that never use them pay no startup cost.

Conformance is partial and experimental — strongest for the APIs that do
not assume a browser or DOM.  ``Intl`` (vendored ICU4C) is available the
same way.  See :ref:`WHATWG / W3C Web Platform APIs (experimental)
<rampart-main:WHATWG / W3C Web Platform APIs (experimental)>`.


Can I run code written for Node.js?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Often, yes.  ``rampart-nodeshim`` is a compatibility layer that makes
``require('fs')``, ``require('path')``, ``require('http')``,
``require('stream')``, ``require('child_process')``,
``require('worker_threads')`` and similar names resolve.

Two things to know:

* Code going through the shim needs the **transpiler** — run with
  ``rampart -t`` (or add a ``"use transpiler"`` pragma) in nearly all
  cases.
* Coverage is partial and the shim is slower than the native APIs.

It exists so that third-party libraries written for Node can run.  For
your own code, prefer ``rampart.utils`` and the ``rampart-*`` modules.
See :ref:`rampart-nodeshim Module
<rampart-extras:rampart-nodeshim Module>` for the per-submodule gaps.


Can I run AI models directly, without the database?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes.  The langtools modules are ordinary ``require()``\ s, independent of
the SQL embedding path described under `Database and Search`_:

* ``rampart-llamacpp`` — ``initEmbed()``, ``initRerank()`` and
  ``initGen()`` for text generation, chat and tool calling.  GPU via
  Metal or CUDA.
* ``rampart-onnx`` — ``initEmbed()`` / ``initRerank()``, plus
  ``initSession()`` to run an arbitrary ONNX model, and tokenizers.
  GPU is CUDA-only.
* ``rampart-clip`` — CLIP image and text embeddings in a shared space.
* ``rampart-faiss`` — a standalone vector index (``openFactory()``,
  ``addFp32()``, ``searchFp32()``), separate from SQL vector indexes.
* ``rampart-ocr`` — PP-OCR: text, box geometry and per-line confidence
  from scans, screenshots and multi-page TIFFs, with layout-aware
  reading order.
* ``rampart-sentencepiece`` — SentencePiece tokenization and
  detokenization.
* ``rampart-models`` — resolve a short model name to a file, downloading
  it on first use: ``models.get("bge-m3:q8_0")``.

All are documented in :ref:`rampart-langtools
<rampart-langtools:The rampart-langtools modules>`.


Can I use a newer or larger LLM than the bundled llama.cpp?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Yes, through ``rampart-llm``.  ``rampart-llamacpp`` carries its own build
of llama.cpp, and because embedding and reranking model formats change
slowly, that build stays current for what it and ``rampart-sql`` are
there to do.  Text generation is the fast-moving end: a brand-new model
may want a llama.cpp newer than the one in your Rampart, and a large one
may belong on another machine entirely.

``rampart-llm`` covers both cases with one streaming API and one response
shape over ``llama-server``, Ollama, any OpenAI-compatible endpoint,
Anthropic's API and the Claude Code CLI:

.. code-block:: javascript

    var llm = require("rampart-llm.js");

    var client = new llm.llamaCpp({server: "10.0.0.5", port: 8080});

Run ``llama-server`` on whatever schedule and whichever host suits you;
the calling code does not change.  It handles SSE parsing, reasoning
output, tool calls, cancellation and context-window discovery.  See
:ref:`rampart-llm <rampart-extras:rampart-llm>`.


Can I build desktop apps or drive a headless browser?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Both, with two different modules:

* ``rampart-webview`` opens a native OS webview — WebKitGTK on Linux,
  WKWebView on macOS — and drives it from JavaScript.  The page is your
  UI, and ``w.bind()`` makes a Rampart function callable from it as
  ``window.<name>()``.  It also exposes a JavaScriptCore context
  (``JSCContext``) for running browser JavaScript libraries in-process.
* ``rampart-chromeview`` is a Puppeteer-compatible client for headless
  Chrome over the DevTools protocol: navigate, wait on selectors,
  ``$eval``, screenshots, PDFs, request interception and raw CDP
  sessions.  Use it for scraping rendered pages and automated testing,
  not for shipping a GUI.

Both are installed with ``rampart --install``.


Is there peer-to-peer networking?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``rampart --install rampart-iroh`` adds
`iroh <https://www.iroh.computer/>`_ — encrypted QUIC connections,
document sync, gossip pub/sub and blob transfer between peers with no
server in the middle.  The package also installs ``iroh-webproxy``, which
exposes a web server running behind NAT through an encrypted P2P tunnel;
``rampart-webserver`` can start it for you with ``--irohProxy``.


Vibe Coding with Rampart
------------------------

How do I use an LLM coding assistant to build Rampart applications?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rampart includes an ``overview.md`` file in its documentation that is
designed for LLM coding assistants.  Point the assistant at the
documentation and the template web server, and describe what you want.

Here is an example using Claude Code to build a full content management
system from scratch:

.. code-block:: bash

    git clone https://github.com/aflin/rampart_docs.git
    mkdir cms
    cd cms
    claude

Then give it a prompt like:

.. code-block:: none

    Please use rampart to create a content management system that uses
    codemirror to edit raw html, jodit as a wysiwyg editor, both included
    from a cdn.  It should be friendly, easy to use, password protected and
    integrated with the webserver to serve the content it builds as well as
    allow management and editing from a separate endpoint.  You may use any
    appropriate databasing system available in rampart, but also use
    rampart-sql to make all content searchable, and have an option in the
    cdn to add a standard site search.
    For instructions on how to use rampart, see the documentation in
    /usr/local/src/rampart_docs/source/ and start with the overview.md file.
    For a template web server setup, use /usr/local/src/rampart/web_server/.

The assistant reads the documentation, sets up the project structure from
the template, and builds the application.  In this case, it produced a
working CMS with the following structure:

.. code-block:: none

    cms/
    ├── web_server_conf.js    # Server config
    ├── apps/
    │   ├── cms_db.js         # Database layer (rampart-sql + rampart-crypto)
    │   ├── admin.js          # Admin interface (auth, page editor)
    │   ├── site.js           # Public content server
    │   └── search.js         # Search API endpoint
    ├── html/                 # Static files
    ├── data/cmsdb/           # SQL database
    └── logs/                 # Access + error logs

Features built automatically:

* Admin panel with password-protected cookie sessions
* Dual editor: visual (Jodit WYSIWYG) and HTML code (CodeMirror), both
  from CDN, tab-switchable with sync
* Page management: create, edit, delete, publish/draft, sort order
* Full-text search via ``rampart-sql`` LIKEP with highlighted results
* Public site with navigation and search bar
* Start/stop/restart via ``rampart web_server_conf.js start``

The key is pointing the assistant at ``overview.md`` as the starting
point and the ``web_server/`` template for project structure.  The
overview gives the LLM enough context to use Rampart's APIs correctly
without writing Node.js code.
