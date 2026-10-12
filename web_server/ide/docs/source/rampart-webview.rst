The rampart-webview module
==========================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-webview module embeds the
`webview <https://github.com/webview/webview>`_ library, a tiny
cross-platform abstraction over each operating system's native
browser engine.  The authors of Rampart extend their thanks to the
webview project and to the underlying engine vendors.  The library
binds to:

* **Linux** — GTK + WebKitGTK

* **macOS** — Cocoa + WebKit

The headless JavaScript features described below are provided by the
JavaScriptCore (JSC) engine that ships with WebKitGTK on Linux and
with the system ``JavaScriptCore.framework`` on macOS.

License
~~~~~~~

The rampart-webview module is released under the MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-webview module provides two related capabilities:

* **A native webview** — an in-process desktop window backed by the
  operating system's own browser engine, for building graphical
  applications whose user interface is written in HTML, CSS, and
  JavaScript.  Rampart functions can be bound so that page
  JavaScript calls back into the host, and the host can inject and
  evaluate JavaScript inside the page.

* **A headless JavaScript engine** — direct access to the
  JavaScriptCore engine bundled with WebKit, allowing Rampart to run
  modern JavaScript (ES2020+) for libraries that need features beyond
  Duktape's ES5.1 support.  No window is created; this is a pure
  data-transformation engine.

The
:ref:`rampart-chromeview <rampart-chromeview:The rampart-chromeview module>`
module is the complementary tool for the inverse case: instead of
embedding a browser engine in-process to build a GUI,
rampart-chromeview drives a separate Chromium process over the
Chrome DevTools Protocol for automation, scraping, server-side
rendering, and headless PDF or screenshot generation.

Typical applications include:

* Desktop GUI applications with an HTML/CSS/JavaScript front end and
  a Rampart back end.

* Local dashboards and tools that present a rampart-server
  application in a dedicated window.

* Automated screenshot generation and visual testing (combined with
  a virtual display on Linux).

* Running data-transformation libraries that require a modern
  JavaScript engine, without a GUI.

How does it work?
~~~~~~~~~~~~~~~~~

The ``WebView`` constructor creates a native window and wires up a
bidirectional bridge between Rampart's Duktape engine and the
browser's JavaScript engine.  Values crossing the bridge — both
arguments to bound functions and results of evaluated expressions —
are carried by a tagged-JSON protocol that preserves rich types
(Dates, Buffers, RegExps, Maps, Sets, ``NaN``/``Infinity``,
``undefined``, and cyclic references) in both directions.

``w.run()`` enters the platform's native event loop and **blocks**
until the window is closed.  All setup — binding functions,
registering event handlers, loading content — must therefore be done
before ``run()`` is called.

The headless ``JSCContext`` and ``jscExec()`` interfaces do not
create a window and do not enter an event loop; they evaluate code in
a JavaScriptCore context and return results immediately.

Loading and Using the Module
----------------------------

Prerequisites
~~~~~~~~~~~~~

The module is built against the host's native browser engine, so the
appropriate development packages must be present to build it, and the
corresponding runtime must be present to use it.

**Linux (Debian/Ubuntu):**

.. code-block:: bash

    sudo apt install libgtk-3-dev libwebkit2gtk-4.1-dev

**macOS:** WebKit ships with the operating system; only the Xcode
command line tools are required to build.

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()``
function:

.. code-block:: javascript

    var webview = require("rampart-webview");

Quick Start
~~~~~~~~~~~

A complete script that opens a window, binds a host function the page
can call, and runs until the window is closed:

.. code-block:: javascript

    var webview = require("rampart-webview");

    var w = new webview.WebView({
        title:  "Hello",
        width:  480,
        height: 320,
        html:   '<button onclick="window.quit()">Close me</button>'
    });

    w.bind("quit", function() {
        w.terminate();
    });

    w.run();        /* blocks until the window closes */
    w.destroy();

The WebView GUI
---------------

Creating a WebView
~~~~~~~~~~~~~~~~~~

.. code-block:: javascript

    var w = new webview.WebView({
        title:  "My App",          // window title (default: "Rampart WebView")
        width:  800,               // width in pixels (default: 800)
        height: 600,               // height in pixels (default: 600)
        debug:  true,              // enable browser developer tools (default: false)
        html:   "<h1>Hello</h1>",  // initial HTML content
        // url: "https://example.com"  // OR navigate to a URL
    });

All options are optional.  If neither ``html`` nor ``url`` is
supplied, the window opens with a blank page.

Methods
~~~~~~~

``w.setTitle(title)``
'''''''''''''''''''''

Update the window title.

.. code-block:: javascript

    w.setTitle("New Title");

``w.setSize(width, height [, hint])``
'''''''''''''''''''''''''''''''''''''

Update the window size.  The optional ``hint`` controls the sizing
behavior and may be a string (``"none"``, ``"min"``, ``"max"``,
``"fixed"``) or the equivalent constant (see `Constants`_).

.. code-block:: javascript

    w.setSize(1024, 768);           // set default size
    w.setSize(400, 300, "min");     // set minimum size
    w.setSize(1920, 1080, "max");   // set maximum size
    w.setSize(640, 480, "fixed");   // fixed (non-resizable) size

``w.navigate(url)``
'''''''''''''''''''

Navigate to a URL.

.. code-block:: javascript

    w.navigate("https://example.com");

``w.setHtml(html)``
'''''''''''''''''''

Load HTML content directly.

.. code-block:: javascript

    w.setHtml("<h1>Hello World</h1>");

``w.init(js)``
''''''''''''''

Inject JavaScript that runs automatically **before every page
load**.  Useful for installing global functions or polyfills that
each page should see.

.. code-block:: javascript

    w.init("window.myGlobal = 'available on every page';");

``w.eval(js [, callback])``
'''''''''''''''''''''''''''

Evaluate JavaScript inside the webview.  ``js`` is always a string;
it executes in the browser's JavaScript engine, not in Duktape.

In the fire-and-forget form, no value is returned:

.. code-block:: javascript

    w.eval("document.title = 'Changed from Rampart';");

When a callback is supplied, the value of the last expression is
captured and passed to the callback as ``(result, error)``.  Rich
types are preserved through the tagged-JSON protocol, and Promises
are awaited automatically:

.. code-block:: javascript

    w.eval("document.title", function(title, err) {
        if (err) console.log("eval error:", err.message);
        else     console.log("title:", title);
    });

    /* Promises are awaited automatically */
    w.eval("fetch('/api/data').then(r => r.json())", function(data, err) {
        // ...
    });

Evaluation should be triggered after the page has loaded (see
``w.on("load", ...)`` below).  Evaluating before a page is loaded does
nothing.

``w.getContents(callback)``
'''''''''''''''''''''''''''

Fetch the current DOM as an HTML string and deliver it to the
callback.  This is shorthand for
``w.eval("document.documentElement.outerHTML", callback)``.

.. code-block:: javascript

    w.on("load", function() {
        w.getContents(function(html, err) {
            rampart.utils.fprintf("page.html", "%s", html);
        });
    });

``w.on(event, handler)``
''''''''''''''''''''''''

Subscribe to a webview event.  ``on()`` must be called before
``run()`` — and before the first ``setHtml()``/``navigate()`` if you
want to receive events from the initial page load.  Multiple handlers
may be registered for the same event; they are called in registration
order.

Two events are supported:

**"load"** fires when a page finishes loading.  The handler receives
the page URL:

.. code-block:: javascript

    w.on("load", function(url) {
        console.log("page loaded:", url);
    });

**"console"** fires for every ``console.log/warn/error/info/debug``
call made inside the webview.  The handler receives the level and an
array of stringified arguments:

.. code-block:: javascript

    w.on("console", function(level, args) {
        console.log("[page " + level + "]", args.join(" "));
    });

``w.bind(name, callback)``
''''''''''''''''''''''''''

Bind a Rampart (Duktape) function so it can be called from
JavaScript running inside the webview.  The bound function appears as
``window.<name>()`` in the page and returns a Promise.

.. code-block:: javascript

    w.bind("add", function(a, b) {
        return a + b;
    });

From within the page:

.. code-block:: html

    <script>
    async function doAdd() {
        var result = await window.add(3, 4);
        console.log(result); // 7
    }
    </script>

The callback can accept and return rich types: numbers, strings,
objects, arrays, booleans, ``null``, Dates, Buffers/ArrayBuffers,
TypedArrays, RegExps, Maps, Sets, ``NaN``, ``Infinity``, and
``undefined``.  Cyclic object references are preserved.  If the
callback throws, the Promise in the page is rejected with the error
message.

``w.unbind(name)``
''''''''''''''''''

Remove a previously bound function.

.. code-block:: javascript

    w.unbind("add");

``w.run()``
'''''''''''

Start the webview event loop.  **This call blocks** until the window
is closed, either by the user or by a call to ``w.terminate()`` from
a bound callback.  All setup must be completed before ``run()`` is
called.

.. code-block:: javascript

    w.run();
    // Execution resumes here after the window is closed

``w.terminate()``
'''''''''''''''''

Stop the event loop, causing ``w.run()`` to return.  Typically called
from within a bound callback:

.. code-block:: javascript

    w.bind("quit", function() {
        w.terminate();
    });

``w.destroy()``
'''''''''''''''

Explicitly destroy the webview instance and free its resources.  This
is also done automatically by the garbage collector when the object
goes out of scope.

.. code-block:: javascript

    w.destroy();

``w.snapshot([region])``
''''''''''''''''''''''''

Capture the webview's rendered content as a PNG image and return it
as a ``Buffer``.  Available on Linux (via WebKitGTK) and macOS (via
WKWebView).

The optional ``region`` argument may be ``"full"`` to capture the
entire document, including content scrolled out of view.  The default
captures only the visible window region.

.. code-block:: javascript

    var png     = w.snapshot();        // visible region only
    var fullPng = w.snapshot("full");  // entire document
    rampart.utils.fprintf("page.png", "%s", png);

Snapshots are typically taken inside a bound callback after the page
has finished loading:

.. code-block:: javascript

    w.bind("ready", function() {
        var png = w.snapshot();
        // ... save or process png ...
        w.terminate();
    });
    w.setHtml('<body><h1>Hello</h1>\
        <script>window.addEventListener("load",function(){window.ready()})</script>\
        </body>');
    w.run();

Combined with the ``headless.sh`` helper (see `Running Without a
Display`_), this enables automated screenshot generation and visual
testing without a GUI session.

``w.setCookie(name, value)``
''''''''''''''''''''''''''''

Set a cookie in the webview's cookie store for the current page's
host.  Available on Linux (WebKitGTK cookie manager) and macOS
(WKHTTPCookieStore).  The page must be
loaded over a real URL scheme (``http://``, ``https://``); pages at
``about:blank`` or behind ``data:`` URIs cannot hold cookies.

.. code-block:: javascript

    w.on("load", function() {
        w.setCookie("session", "abc123");
        w.setCookie("user", "alice");
    });
    w.navigate("https://example.com/");

``w.getCookies([uri])``
'''''''''''''''''''''''

Return the cookies that would be sent for ``uri`` — or for the
current page's URI when ``uri`` is omitted — as a plain object of
``{name: value, ...}``.  Host-suffix, path-prefix, and secure-flag
filtering are applied so the result matches what the browser would
actually attach to a request.

Available on Linux and macOS.  Throws if no URI is passed and no page
has been loaded yet.

.. code-block:: javascript

    w.on("load", function() {
        var cookies = w.getCookies();
        console.log(cookies.session);   // "abc123"

        /* Cookies for a different origin: */
        var other = w.getCookies("https://other.example.com/path");
    });

``w.getAllCookies()``
'''''''''''''''''''''

Return every cookie currently in the webview's cookie store as a
plain object of ``{name: value, ...}``, with no host/path/secure
filtering.  This is useful when the page was loaded via ``setHtml()``
(and so has no resolvable host for ``getCookies()`` to filter
against), or when you want to inspect the full store across origins.

Available on macOS, and on Linux when the underlying WebKitGTK ABI
exports ``webkit_cookie_manager_get_all_cookies`` (true on Debian
Bullseye and newer, and on modern macOS).  On Debian Buster's stock
WebKitGTK the property is simply absent from the webview object;
guard with ``typeof w.getAllCookies === "function"`` if you need to
support that platform.

.. code-block:: javascript

    w.on("load", function() {
        if (typeof w.getAllCookies === "function") {
            var everything = w.getAllCookies();
            console.log(Object.keys(everything).length, "cookies in store");
        }
    });

``w.setUserAgent(string)``
''''''''''''''''''''''''''

Set the User-Agent string sent with HTTP requests and returned by
``navigator.userAgent``.  Available on Linux and macOS.  Call before
``navigate()`` to affect the first request.

.. code-block:: javascript

    w.setUserAgent("MyApp/1.0");
    w.navigate("https://example.com/");

Constants
~~~~~~~~~

The window-sizing hints accepted by ``setSize()`` are available both
as strings and as module constants.

.. list-table::
   :header-rows: 1
   :widths: 30 10 60

   * - Constant
     - Value
     - Description
   * - ``webview.HINT_NONE``
     - 0
     - Default window size
   * - ``webview.HINT_MIN``
     - 1
     - Minimum size constraint
   * - ``webview.HINT_MAX``
     - 2
     - Maximum size constraint
   * - ``webview.HINT_FIXED``
     - 3
     - Fixed (non-resizable) size

Accessing Local Servers
~~~~~~~~~~~~~~~~~~~~~~~~

If a webview page needs to reach a local HTTP server (for example
``http://127.0.0.1:8088/api/data.json``), the browser's same-origin
policy may block the request depending on how the page was loaded.
Pages loaded via ``setHtml()`` or a ``file://`` URL have a ``null``
origin, so ``fetch``/``XHR`` requests to ``http://`` URLs are treated
as cross-origin.  There are three ways to handle this.

Option 1: Serve the page from the same server
'''''''''''''''''''''''''''''''''''''''''''''

If you are running
:ref:`rampart-server <rampart-server:The rampart-server HTTP module>`,
navigate to it directly.  Page and API then share an origin and no
CORS issue arises:

.. code-block:: javascript

    w.navigate("http://127.0.0.1:8088/myapp/index.html");

Option 2: Add CORS headers on the server
''''''''''''''''''''''''''''''''''''''''

Configure the local server to send permissive CORS headers, allowing
pages loaded via ``setHtml()`` or ``file://`` to make requests:

.. code-block:: javascript

    // In your rampart-server route handler:
    return { json: data, headers: {"Access-Control-Allow-Origin": "*"} };

Option 3: Use ``w.bind()`` as a proxy
'''''''''''''''''''''''''''''''''''''

Expose a Rampart function that performs the HTTP request on the host
side, bypassing browser security entirely.  This is the most flexible
approach and works regardless of how the page was loaded:

.. code-block:: javascript

    // Rampart side
    var curl = require("rampart-curl");

    w.bind("fetchJson", function(url) {
        var res = curl.fetch(url);
        return JSON.parse(res.body);
    });

.. code-block:: html

    <!-- Webview side -->
    <script>
    async function loadData() {
        var data = await window.fetchJson("http://127.0.0.1:8088/apps/myapp.json");
        console.log(data);
    }
    </script>

The request happens in Rampart, free of browser restrictions, and
the result is returned to the page as a resolved Promise.

Running Without a Display
~~~~~~~~~~~~~~~~~~~~~~~~~

The webview requires a display connection (X11 on Linux, the
WindowServer on macOS).  For automated tasks that do not need a
visible window — generating screenshots, running bound callbacks — a
virtual display can be used.

**Linux:** The included ``headless.sh`` script wraps rampart with
Xvfb (the X virtual framebuffer).  Install Xvfb first:

.. code-block:: bash

    sudo apt install xvfb                    # Debian/Ubuntu
    sudo dnf install xorg-x11-server-Xvfb    # Fedora/RHEL

Then run any webview script headlessly:

.. code-block:: bash

    ./headless.sh my_script.js
    ./headless.sh --dim 1920x1080 my_script.js

The webview renders fully inside the virtual display; ``snapshot()``
and all bound callbacks work normally.

**macOS:** Headless operation is not directly supported, because
Cocoa applications require an active WindowServer (a login session).
CI services such as GitHub Actions and CircleCI handle this by
keeping a user logged in on their macOS runners.

Headless JavaScriptCore
-----------------------

In addition to the GUI webview, the module provides direct access to
the JavaScriptCore (JSC) engine bundled with WebKit, so Rampart can
execute modern JavaScript (ES2020+) without creating a window.  This
gives Rampart a full JIT-compiled JS engine for running third-party
libraries that need features beyond Duktape's ES5.1 support.

The JSC features are available on Linux (via WebKitGTK) and macOS
(via the system ``JavaScriptCore.framework``).

Two interfaces are provided: ``jscExec()`` for one-shot evaluation,
and ``JSCContext`` for a persistent interpreter where you can load
modules, maintain state, and call methods on JSC objects directly.

Compatibility
~~~~~~~~~~~~~

The JSC context is a pure JavaScript engine.  It provides ECMAScript
builtins (``Math``, ``JSON``, ``Date``, ``Promise``, ``Map``,
``Set``, ``RegExp``, TypedArrays, etc.) but does **not** include Web
Platform APIs (``fetch``, ``setTimeout``, ``WebSocket``,
``localStorage``, the DOM, etc.) or Node.js APIs (``fs``, ``http``,
``require``, etc.).  Promises and ``async``/``await`` work correctly;
the microtask queue drains between calls.

As a general rule: if a library's job is **transforming data** rather
than performing I/O or rendering, it will work.  The following
libraries have been tested and verified: lodash, mathjs, marked, ajv,
papaparse, handlebars, js-yaml, fuse.js, and validator.js.  Libraries
that need network access, timers, a DOM, or Node.js modules will not
work.  Use Rampart's own modules (rampart-curl, rampart-server,
rampart-lmdb, etc.) for I/O, and pass data into JSC via ``jsc.set()``
for processing.

**On callbacks:** Duktape functions cannot be called from within JSC
— the two are separate engines.  Data values pass freely in both
directions, but any operation that requires a *callback function* as
an argument must be performed inside ``jsc.eval()``, where the
callback is itself a JSC function.  Calling JSC methods with simple
arguments (strings, numbers, objects, arrays) works directly from
Rampart.

``webview.jscExec(code)``
~~~~~~~~~~~~~~~~~~~~~~~~~~

Evaluate JavaScript in a temporary JSC context and return the result.
A fresh context is created and destroyed on each call.  The return
value is deep-converted to a native Duktape value using the rich type
mapping below:

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - JSC type
     - Duktape result
   * - number, string, boolean, null, undefined
     - Native equivalent (including ``NaN``, ``Infinity``)
   * - Date
     - Duktape Date (``instanceof Date`` works)
   * - ArrayBuffer
     - Node.js-style Buffer
   * - TypedArray (Uint8Array, Float64Array, etc.)
     - Matching Duktape TypedArray
   * - RegExp
     - Duktape RegExp (source + flags preserved)
   * - Error / TypeError / etc.
     - Duktape Error (name + message preserved)
   * - Map
     - Duktape Map (``instanceof Map``; entries converted recursively)
   * - Set
     - Duktape Set (``instanceof Set``; values converted recursively)
   * - Array, Object
     - Recursive deep conversion
   * - Function
     - Its ``toString()`` source text

.. code-block:: javascript

    var webview = require("rampart-webview");

    webview.jscExec("40 + 2");                       // 42
    webview.jscExec("new Date('2026-01-01')");        // Date object
    webview.jscExec("new Uint8Array([0xCA, 0xFE])");  // TypedArray
    webview.jscExec("/^hello$/gi");                    // RegExp
    webview.jscExec("[...new Set([1,2,2,3])]");        // [1, 2, 3]

Thrown exceptions propagate as Duktape errors:

.. code-block:: javascript

    try {
        webview.jscExec("throw new TypeError('oops')");
    } catch(e) {
        console.log(e.message); // contains "TypeError: oops"
    }

``new webview.JSCContext()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Create a persistent JavaScriptCore context.  State is preserved
across calls, and objects returned from JSC are wrapped in Duktape
proxies so their properties and methods can be accessed directly.

.. code-block:: javascript

    var jsc = new webview.JSCContext();

``jsc.eval(code)``
''''''''''''''''''

Evaluate code in the persistent context and return the value of the
last expression.  Primitives, Dates, RegExps, and Buffers are
auto-converted to native Duktape values; objects and functions are
returned as live proxies backed by the JSC engine.

.. code-block:: javascript

    jsc.eval("var x = 10;");
    jsc.eval("x * 4");            // 40 (state persists)

    var obj = jsc.eval("({greet: function(n) { return 'Hello ' + n; }})");
    obj.greet("World");            // "Hello World"

``jsc.loadScript(path)``
''''''''''''''''''''''''

Read a JavaScript file and evaluate it in the persistent context.
Returns the value of the last expression (usually not meaningful for
library scripts).  Use ``getGlobal()`` to reach the globals the
script defines.

.. code-block:: javascript

    jsc.loadScript("/path/to/library.js");
    var lib = jsc.getGlobal("LibraryName");

``jsc.require(path)``
'''''''''''''''''''''

Load a JavaScript file with a CommonJS-compatible shim.  The file is
wrapped in a ``function(module, exports, require){ ... }`` closure and
``module.exports`` is returned.  This works with UMD bundles and
CommonJS modules, which covers most npm packages.  There is also
limited, experimental support for ES module syntax (``export
default``, ``export function``, ``export { ... }``); if the CommonJS
shim fails with a ``SyntaxError``, ``require`` will attempt to
transform ``export`` statements automatically.

Both the CommonJS and ESM paths expect **single-file bundles**.
Multi-file modules with ``import`` or ``require`` dependencies between
files are not resolved — use a bundler (for example
``esbuild lib.js --bundle --format=cjs``) to produce one file first.

.. code-block:: javascript

    var math = jsc.require("/path/to/math.js");
    math.add(2, 3);  // 5

``jsc.set(name, value)``
''''''''''''''''''''''''

Set a global variable in the JSC context.  The value is converted
from Duktape to JSC; numbers, strings, booleans, arrays, objects,
Buffers, Dates, and already-wrapped JSC values are all supported.

.. code-block:: javascript

    jsc.set("config", {debug: true, maxRetries: 3});
    jsc.eval("config.maxRetries");  // 3

``jsc.getGlobal(name)``
'''''''''''''''''''''''

Get a global variable from the JSC context as a wrapped value.

.. code-block:: javascript

    jsc.loadScript("library.js");
    var lib = jsc.getGlobal("Library");
    lib.someMethod();

``jsc.destroy()``
'''''''''''''''''

Destroy the JSC context and free its resources.  Also done
automatically by the garbage collector.

.. code-block:: javascript

    jsc.destroy();

Working with Wrapped JSC Objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Objects returned from ``eval()``, ``require()``, ``getGlobal()``, or
JSC method calls are Duktape proxies that resolve properties lazily
from JSC:

.. code-block:: javascript

    var math = jsc.require("math.js");

    // Property access resolves from JSC
    math.pi;                       // 3.141592653589793

    // Method calls marshal arguments to JSC and convert the result
    math.sqrt(144);                // 12
    math.add(3, 4);                // 7

    // Returned objects are also proxied
    var m = math.matrix([[1, 2], [3, 4]]);
    math.det(m);                   // -2
    math.inv(m).toString();        // "[[-2, 1], [1.5, -0.5]]"

JSC objects passed as arguments to other JSC functions are unwrapped
automatically — no manual conversion is needed:

.. code-block:: javascript

    var c1 = math.complex(3, 4);
    var c2 = math.complex(1, -2);
    math.add(c1, c2).toString();   // "4 + 2i"

``.toValue()``
''''''''''''''

Deep-convert a wrapped JSC object to a plain Duktape value (using the
same rich type mapping as ``jscExec()``).  Useful when you need a
native object for ``JSON.stringify()``, for passing to another
Rampart module, and so on:

.. code-block:: javascript

    var result = math.evaluate("[1, 2, 3]");
    var arr = result.toValue();     // plain Duktape array [1, 2, 3]
    JSON.stringify(arr);            // "[1,2,3]"

``.toString()``
'''''''''''''''

Get the string representation of a wrapped JSC value.

.. code-block:: javascript

    math.matrix([[1, 0], [0, 1]]).toString();  // "[[1, 0], [0, 1]]"

Examples
--------

WebView: Counter App
~~~~~~~~~~~~~~~~~~~~

A small GUI application.  The page calls a bound host function on each
click and a second bound function to quit.

.. code-block:: javascript

    var webview = require("rampart-webview");

    var w = new webview.WebView({
        title: "Counter App",
        width: 400,
        height: 300,
        debug: false,
        html: '<html>\
    <body style="font-family: sans-serif; text-align: center; padding: 40px;">\
      <h1 id="count">0</h1>\
      <button onclick="increment()">Increment</button>\
      <button onclick="window.quit()">Quit</button>\
      <script>\
        var count = 0;\
        async function increment() {\
          count = await window.addOne(count);\
          document.getElementById("count").textContent = count;\
        }\
      </script>\
    </body>\
    </html>'
    });

    w.bind("addOne", function(n) {
        console.log("addOne called:", n);
        return n + 1;
    });

    w.bind("quit", function() {
        w.terminate();
    });

    w.run();
    w.destroy();

JSCContext: Fetching and Using a JS Library
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This example uses
:ref:`rampart-curl <rampart-curl:The rampart-curl module>` to
download mathjs from a CDN, saves it to a file, then loads it into a
``JSCContext`` and runs several operations:

.. code-block:: javascript

    rampart.globalize(rampart.utils);
    var curl = require("rampart-curl");
    var wv   = require("rampart-webview");

    var url  = "https://cdn.jsdelivr.net/npm/mathjs@13.2.2/lib/browser/math.js";
    var file = "/tmp/math.js";

    /* Fetch the library */
    printf("Fetching mathjs from CDN... ");
    var res = curl.fetch({location: true}, url);
    if (res.status !== 200) {
        printf("FAILED (status %d)\n", res.status);
        process.exit(1);
    }
    printf("%d bytes, status %d\n", res.body.length, res.status);

    /* Save to file */
    rampart.utils.fprintf(file, "%s", res.body);
    printf("Saved to %s\n\n", file);

    /* Load into JSC and run some tests */
    var jsc  = new wv.JSCContext();
    var math = jsc.require(file);

    printf("mathjs version: %s\n\n", math.version);

    /* Linear algebra */
    printf("Linear algebra:\n");
    var m = math.matrix([[2, 1, 0], [1, 3, 1], [0, 1, 2]]);
    printf("  matrix:       %s\n", m.toString());
    printf("  determinant:  %s\n", math.det(m));
    printf("  inverse:      %s\n", math.inv(m).toString());

    /* Expression parser */
    printf("\nExpression parser:\n");
    printf("  e^(i*pi) + 1 = %s\n", math.evaluate("e^(i*pi) + 1").toString());
    printf("  3 inches in cm = %s\n", math.evaluate("3 inch to cm").toString());
    printf("  derivative of sin(x)*x^2 = %s\n",
        math.derivative("sin(x)*x^2", "x").toString());

    /* Statistics on data passed from Duktape */
    printf("\nStatistics (data passed from Duktape to JSC):\n");
    var data = [2, 4, 6, 8, 10, 12, 14, 16, 18, 20];
    printf("  data:     %s\n", JSON.stringify(data));
    printf("  mean:     %s\n", math.mean(data));
    printf("  median:   %s\n", math.median(data));
    printf("  std:      %s\n", math.std(data));

    jsc.destroy();
    printf("\nDone.\n");
