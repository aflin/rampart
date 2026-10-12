The rampart-chromeview module
=============================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-chromeview module drives an external `Chromium
<https://www.chromium.org/>`_ (or Google Chrome) process via the
`Chrome DevTools Protocol
<https://chromedevtools.github.io/devtools-protocol/>`_.  The
authors of Rampart extend their thanks to the Chromium project for
the browser engine and the documented DevTools Protocol, and to
the `Puppeteer <https://pptr.dev/>`_ project, whose well-shaped
API surface is the model on which rampart-chromeview's own surface
is patterned.  The WebSocket connection through which CDP traffic
flows is provided by
:ref:`the rampart-net module <rampart-net:The rampart-net module>`.

License
~~~~~~~

The rampart-chromeview module is released under the MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-chromeview module provides programmatic automation
control over a real Chromium browser running as a separate
process.  The exposed API covers navigation, DOM queries,
JavaScript evaluation inside the page, screenshots, PDF
generation, cookies, network interception, mouse and keyboard
event dispatch, iframe traversal, multiple tabs, incognito-like
browser contexts, page-to-host function bindings, drag-and-drop,
and device emulation.

The
`rampart-webview <https://github.com/aflin/rampart-webview>`_
module — which embeds a native webview library in-process — is
intended for building desktop GUI applications.
rampart-chromeview is the complementary module for the inverse
case: a separate Chromium process driven over CDP for automation,
scraping, testing, server-side rendering, and headless PDF or
screenshot generation.

Typical applications include:

* Server-side rendering of a single-page application, capturing
  the finished HTML.

* PDF generation from HTML and CSS sources.

* Web scraping of JavaScript-rendered pages.

* Screenshot automation, including device emulation and full-page
  captures.

* Headless browser testing.

* Data extraction from sites whose content is gated behind a
  single-page-application router.

How does it work?
~~~~~~~~~~~~~~~~~

After the module is loaded, a Chromium process is launched (or an
existing one connected to) via the ``launch()`` or ``connect()``
function.  All subsequent control flows over the Chrome DevTools
Protocol on a WebSocket.

Rampart's libevent loop does not pump during top-level blocking
calls, so a single-threaded design could not satisfy synchronous
calls — the CDP reply would never arrive while the main loop is
parked.  rampart-chromeview addresses this by running the
WebSocket and the CDP dispatcher in a worker thread via
:ref:`rampart.thread <rampart-thread:Rampart Thread Functions>`.
Main-thread synchronous calls block on ``rampart.thread.waitfor``;
the worker unblocks them via ``rampart.thread.put`` once the reply
arrives.

The same API surface supports three calling conventions —
synchronous blocking calls, classic callbacks, and
``async``/``await`` Promises — selected automatically per call
based on the arguments and the runtime mode.

Loading and Using the Module
----------------------------

Prerequisites
~~~~~~~~~~~~~

* **Chromium or Google Chrome** installed on the host.  The module
  searches the following locations by default:

  * ``/usr/bin/google-chrome-stable``

  * ``/usr/bin/google-chrome``

  * ``/usr/bin/chromium``

  * ``/usr/bin/chromium-browser``

  * ``/snap/bin/chromium`` (modern Ubuntu's Snap-packaged Chromium)

  * ``/usr/local/bin/chrome`` (FreeBSD's ``pkg install chromium``)

  * ``/usr/local/bin/chromium`` (FreeBSD, alternate name)

  * ``/Applications/Google Chrome.app/Contents/MacOS/Google Chrome``

  * ``/Applications/Chromium.app/Contents/MacOS/Chromium``

  Passing ``{executablePath: "..."}`` to ``launch()`` overrides the
  search.

  On Debian/Ubuntu:

  .. code-block:: bash

     sudo apt install chromium

  On FreeBSD:

  .. code-block:: bash

     pkg install chromium

* Rampart built with
  :ref:`rampart-net <rampart-net:The rampart-net module>` and
  :ref:`rampart-curl <rampart-curl:The rampart-curl module>` (both
  ship in the standard distribution).

Loading rampart-chromeview
~~~~~~~~~~~~~~~~~~~~~~~~~~

Loading the module is a simple matter of using the ``require()`` function:

.. code-block:: javascript

    var chrome = require("rampart-chromeview");

Quick Start
~~~~~~~~~~~

A complete script that launches headless Chrome, navigates to a
page, captures the title, and writes a full-page PNG screenshot:

.. code-block:: javascript

    rampart.globalize(rampart.utils);
    var chrome = require("rampart-chromeview");

    var browser = chrome.launch({headless: true});
    var page    = browser.newPage();

    page.goto("https://example.com", {waitUntil: "load"});
    printf("title: %s\n", page.title());

    var png = page.screenshot({fullPage: true});
    fprintf("/tmp/example.png", "%s", png);

    browser.close();

Calling Conventions
-------------------

Every CDP-backed method supports three calling styles, selected
automatically by the module at call time.

Synchronous
~~~~~~~~~~~

When no callback is supplied and the transpiler is not in effect,
the call blocks and returns the result directly:

.. code-block:: javascript

    var title = page.title();

Callback
~~~~~~~~

When the last argument is a function, the call returns immediately
and the callback fires on the main event loop once the CDP reply
arrives.  The callback signature is ``(result, error)``:

.. code-block:: javascript

    page.title(function(title, err) {
        if (err) throw err;
        console.log(title);
    });

Promise / async-await
~~~~~~~~~~~~~~~~~~~~~

When no callback is supplied **and** a source transformation is
active for the current script, the call returns a Promise.  Both
of rampart's source transformations are honoured:

* The built-in transpiler, engaged by ``-t``, ``"use transpiler"``,
  or ``"use transpilerGlobally"``.

* Babel, engaged by ``-b``, ``"use babel"``, or
  ``"use babelGlobally"``.

.. code-block:: javascript

    "use transpiler"
    async function main() {
        const title = await page.title();
    }

The module detects the active transformation at call time by
inspecting internal markers that rampart sets when either path is
engaged.  ``Promise`` itself is installed by rampart
unconditionally and is therefore not a useful discriminator.

A single source file may mix synchronous calls and callbacks
freely; transformed code may additionally use ``await``.  Within
a file that has a source transformation active, supplying a
callback as the last argument still selects the callback path —
the dispatcher prefers an explicit callback over Promise return
when both would be valid.

Page-side callbacks: strings always work; functions require "use transpiler"
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Page-side callbacks passed to ``evaluate``, ``$eval``, ``$$eval``,
``waitForFunction``, ``ElementHandle.evaluate``, and similar
methods may be supplied in either form:

* **A source string** — accepted in every runtime mode and
  requires no transpiler.

* **A JavaScript function** — accepted only under
  ``"use transpiler"``, which causes
  ``Function.prototype.toString()`` to return the original source.
  Without the transpiler, the underlying engine returns the stub
  ``"function () { [ecmascript code] }"`` and the function body
  cannot be shipped to Chrome.  The module raises an error in
  that case.

.. code-block:: javascript

    /* accepted in every mode */
    page.$eval("h1", "el => el.textContent");

A function may be passed instead when ``"use transpiler"`` is the first
statement of the file:

.. code-block:: javascript

    "use transpiler"

    await page.$eval("h1", el => el.textContent);
    await page.evaluate(() => document.title);

Event-driven features need the main event loop
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``page.exposeFunction(name, fn)`` and ``page.on("request", ...)``
invoke the Rampart-side handler from the main thread's event loop.
The event loop runs only **after the top-level script finishes**
(or between ``await``\ s in transpiled mode).  Queued handler
invocations will not fire until the chain of top-level blocking
synchronous calls returns.

In practice this means that for ``exposeFunction`` and request
interception, either (a) the script must run in ``"use transpiler"``
mode with ``async``/``await``, or (b) setup is performed at the top
level and the event loop is then allowed to run.

Known Limitations
-----------------

The behaviors documented in this section are areas where
rampart-chromeview deliberately diverges from Puppeteer or carries
a caveat that is worth noting.

Sync mode and event-triggered waits do not mix
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``page.waitForResponse(urlMatch)``, ``waitForRequest``,
``waitForFileChooser``, ``waitForNavigation``, and the page event
handlers all rely on the main event loop running.  In sync mode the
calling thread is blocked, so if the action that triggers the event
runs on **the same thread, after the wait** call, the call will
deadlock:

.. code-block:: javascript

    /* Will deadlock — sync mode blocks the thread, so the fetch
       never runs. */
    var resp = page.waitForResponse("/api/x");
    page.evaluate("fetch('/api/x')");

The remedies are: use ``"use transpiler"`` together with
``async``/``await``, trigger the action from a different thread, or
pass a callback to ``waitForResponse``.

Function.prototype.toString returns a stub in plain Duktape
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``Page``/``Frame``/``ElementHandle`` evaluate-family methods
accept either source strings or functions.  Functions only carry
source under ``"use transpiler"`` (or the ``-t`` CLI flag).  In
plain ES5 mode, ``fn.toString()`` returns
``"function () { [ecmascript code] }"`` and the module throws an
error indicating that a source string is required.

Worker — Runtime only, no DOM
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``Worker.evaluate`` runs in the worker's global scope via
``Runtime.evaluate``.  Workers have no DOM, so ``$``, ``$$``,
``evaluateHandle`` and similar are intentionally not exposed on
``Worker``.

page.queryObjects requires a real JSHandle prototype
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``page.evaluateHandle("Foo.prototype")`` is passed in, not a class
name string.  This matches Puppeteer.

ConsoleMessage shape
~~~~~~~~~~~~~~~~~~~~

The ``console`` event handler signature is ``(ConsoleMessage)`` —
``m.type()``, ``m.text()``, ``m.args()``, ``m.location()``.  This
matches Puppeteer.

Cleanup of launched Chrome on script exit
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``launch()`` registers a Duktape finalizer on the returned
``Browser`` that fires on heap destruction — covering
``process.exit()``, ``SIGTERM``, ``SIGINT``, parse errors, and
natural end-of-script.  If ``browser.close()`` was not called
before the script exited, Chrome is killed and the temporary
``userDataDir`` is removed.  This is best-effort: ``SIGKILL`` or
segfault skips finalizers, so a ``try``/``finally`` around
``browser.close()`` remains the most reliable shutdown.

Browsers obtained via ``connect()`` are intentionally left alone.
The module did not launch Chrome and does not assume ownership of
the process.

HTML5 drag-and-drop requires explicit interception
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``page.mouse.dragAndDrop`` defaults to mouse-event click-and-drag
(which works for many "drag a slider thumb" UIs) but does **not**
fire HTML5 ``dragstart``/``dragover``/``drop`` events.  To exercise
HTML5 drag-and-drop (file uploads via dropzones, sortable lists,
kanban boards), ``page.setDragInterception(true)`` must be called
first.  Subsequent calls to ``mouse.dragAndDrop`` (or
``ElementHandle.dragAndDrop``) then dispatch the HTML5 path.

Module-Level Functions
----------------------

Once loaded, the rampart-chromeview module exposes these top-level
entry points.

chrome.launch()
~~~~~~~~~~~~~~~

Launch a new Chrome child process and connect to it.

Usage:

.. code-block:: javascript

    var chrome = require("rampart-chromeview");

    var browser = chrome.launch([options][, callback]);

Where:

* ``options`` is an optional :green:`Object` with the following
  properties:

  * ``headless`` - a :green:`Boolean`.  When ``true`` (the default),
    Chrome runs without a visible window.  Pass ``false`` for a
    visible browser.

  * ``executablePath`` - a :green:`String`.  The Chrome binary to
    launch.  Auto-located from the candidate list under
    `Prerequisites`_ if omitted.

  * ``userDataDir`` - a :green:`String`.  Chrome's profile
    directory.  If omitted, a fresh temporary directory is created
    and cleaned up automatically on ``browser.close()``.

  * ``args`` - an :green:`Array` of :green:`String`\ s.  Additional
    Chrome command-line flags.

* ``callback`` is an optional :green:`Function`.  If given, the
  call is non-blocking and the callback is invoked with
  ``(browser, error)``.

Return Value:
  A `Browser`_ object.

Throws if Chrome fails to start within 15 seconds.

chrome.connect()
~~~~~~~~~~~~~~~~

Attach to an already-running Chrome instance.

Usage:

.. code-block:: javascript

    /* By HTTP endpoint (the module fetches /json/version for the ws URL) */
    var browser = chrome.connect({browserURL: "http://127.0.0.1:9222"});

    /* Or by WebSocket URL directly */
    var browser = chrome.connect({
        browserWSEndpoint: "ws://127.0.0.1:9222/devtools/browser/abc-123"
    });

Where ``options`` is one of:

* ``{browserURL: "http://host:port"}`` - The module fetches
  ``/json/version`` from this URL to derive the WebSocket
  endpoint.

* ``{browserWSEndpoint: "ws://host:port/devtools/browser/..."}`` -
  Connect directly to the given WebSocket.

Return Value:
  A `Browser`_ object.  The browser will **not** be killed when
  ``browser.close()`` is later called — only the WebSocket is
  disconnected.

chrome.executablePath()
~~~~~~~~~~~~~~~~~~~~~~~

Returns the absolute path to the Chrome/Chromium binary that
``launch()`` would use.  Optionally pass a :green:`String`
``override`` to validate that a custom path exists.  Throws if
nothing is found.

.. code-block:: javascript

    var path = chrome.executablePath();
    var path = chrome.executablePath("/opt/chromium/chrome");

chrome.defaultArgs()
~~~~~~~~~~~~~~~~~~~~

Returns the flag :green:`Array` that ``launch()`` would pass to
Chrome.  Intended for use in debugging and in wrapper functions
that need to inspect, augment, or filter the default flag set
before launching.

.. code-block:: javascript

    var args = chrome.defaultArgs({headless: true});

chrome.devices
~~~~~~~~~~~~~~

A registry of common device descriptors keyed by name, usable with
`page.emulate()`_:

* ``"iPhone SE"``, ``"iPhone 12"``, ``"iPhone 12 Pro"``,
  ``"iPhone 13 Pro Max"``, ``"iPhone 14"``

* ``"iPad"``, ``"iPad Pro"``

* ``"Pixel 5"``, ``"Pixel 7"``

* ``"Galaxy S20"``, ``"Galaxy S20 Ultra"``

chrome.networkConditions
~~~~~~~~~~~~~~~~~~~~~~~~

Preset network conditions keyed by name: ``"Slow 3G"``,
``"Fast 3G"``, ``"Slow 4G"``, ``"Fast 4G"``, ``"Offline"``.  Pass
either the preset name or a custom :green:`Object` to
`page.emulateNetworkConditions()`_.

chrome.TimeoutError
~~~~~~~~~~~~~~~~~~~

A subclass of ``Error``.  Thrown (or rejected) by
``waitForSelector``, ``waitForFunction``, ``waitForRequest``,
``waitForResponse``, and similar wait-family methods when their
timeout elapses.  Use ``err instanceof chrome.TimeoutError`` (or
``err.name === "TimeoutError"``) to distinguish from other
failures.

Browser
-------

Returned from `chrome.launch()`_ or `chrome.connect()`_.
Represents the whole Chrome process.

Every method that takes an optional ``[, cb]`` last argument
supports the three calling conventions described above (sync,
callback, Promise).

* ``browser.pages([cb])`` - Returns every open page across all
  browser contexts, including the initial ``about:blank`` and any
  pages opened by chrome itself.  Pages not already attached to are
  attached transparently and tracked as `Page`_ instances.

* ``browser.targets([cb])`` - Returns every CDP target as a
  `Target`_ (pages, service workers, browser-level targets, etc.).

* ``browser.version([cb])``, ``browser.userAgent([cb])`` -
  Strings from ``Browser.getVersion``.  ``version()`` returns e.g.
  ``"HeadlessChrome/147.0.0.0"``.

* ``browser.isConnected()`` - Returns ``true`` while the worker
  thread is still alive.

* ``browser.disconnect([cb])`` - Closes the WebSocket and worker
  but leaves the Chrome process running.  Appropriate for
  ``connect()`` flows and when transferring control of the browser
  to another tool.

* ``browser.process()`` - For browsers started with ``launch()``,
  returns ``{pid}`` for the Chrome child process.  Returns
  ``null`` for ``connect()``\ -obtained browsers.

* ``browser.newPage([cb])`` - Open a new tab in the default
  browser context.  Returns a `Page`_.

* ``browser.createBrowserContext([cb])`` - Create an isolated
  (incognito-like) browser context with its own cookie jar and
  storage.  Returns a `BrowserContext`_.

* ``browser.defaultBrowserContext()`` - Return the default
  `BrowserContext`_.  Synchronous.

* ``browser.wsEndpoint()`` - Return the WebSocket URL of the
  current CDP connection.  Synchronous.

* ``browser.close([cb])`` - Shut down the worker thread.  If the
  browser was launched by ``launch()``, also kill the Chrome
  process and remove the temporary user-data directory.

* ``browser.fetch()``, ``browser.fetchAsync()``,
  ``browser.fetchClose()`` - Retrieve URLs through Chrome and get
  back a ``curl.fetch()``\ -style result.  See `Fetching URLs`_
  below.

* ``browser.download()`` - Save a URL to a local file, letting Chrome
  write it.  See `browser.download()`_ below.

Browser events
~~~~~~~~~~~~~~

``browser.on(event, handler)``, ``browser.off(event, handler)``,
``browser.once(event, handler)`` register/unregister handlers for
browser-level events.  Subscribing to any of these events enables
``Target.setDiscoverTargets`` automatically.

* ``"targetcreated"`` - handler args: ``(Target)``

* ``"targetdestroyed"`` - handler args: ``(Target)``

* ``"targetchanged"`` - handler args: ``(Target)``

* ``"disconnected"`` - handler args: ``()``.  Fired on
  ``browser.close()``, ``disconnect()``, or when rampart-net's
  WebSocket ping/pong detects an ungracefully-exited Chrome
  (~30 seconds after the process dies).

Fetching URLs
~~~~~~~~~~~~~

``browser.fetch()`` and ``browser.fetchAsync()`` retrieve URLs
through Chrome and return the same kind of result as
:ref:`rampart-curl <rampart-curl:The rampart-curl module>`'s
``fetch()`` and ``fetchAsync()``.  Use them in place of curl when a
page must be rendered by a real browser — for example, a page
built by JavaScript — or when a site only answers a browser.

* HTML pages are loaded and rendered.  ``text`` holds the rendered
  page (the DOM after scripts have run) and ``body`` holds the
  original bytes sent by the server.

* Anything else (images, archives, JSON, PDFs, plain text, etc.) is
  returned byte-for-byte in ``body`` without being opened in the
  browser.  Files are never saved to Chrome's download directory.

* Fetches use their own tabs in a private browser context.  They
  are not returned by ``browser.pages()`` or ``browser.targets()``,
  do not fire ``"targetcreated"`` events, and do not share cookies
  with pages opened by ``browser.newPage()``.

* Tabs are created as needed and reused.  Cookies, local storage and
  the HTTP cache carry over from one fetch to the next, as in a
  normal browser session.  Use the ``fresh`` option or
  ``browser.fetchClose()`` to start clean.

browser.fetch()
"""""""""""""""

Usage:

.. code-block:: javascript

    var res = browser.fetch(url[, options]);

    browser.fetch(url_list[, options], callback);

Where:

* ``url`` is a :green:`String`, the URL to retrieve.

* ``url_list`` is an :green:`Array` of URLs.  A callback is required.

* ``options`` is an optional :green:`Object`.  See
  `Fetch Options`_ below.

* ``callback`` is a :green:`Function` that receives one argument, a
  `Fetch Results`_ :green:`Object`.  It is called once per URL, in
  the order the fetches complete.  Inside the callback,
  ``this.addurl(url)`` adds another URL to the list.  Returning
  ``false`` stops processing further results.

``fetch()`` blocks until every URL has been retrieved.

Return Value:
  With a single ``url`` and no callback, a `Fetch Results`_
  :green:`Object`.  Otherwise ``undefined``.

browser.fetchAsync()
""""""""""""""""""""

The same as ``browser.fetch()`` except that it returns immediately
and the callback runs in the Rampart event loop.

Usage:

.. code-block:: javascript

    browser.fetchAsync(url|url_list[, options], callback)
        .finally(function() { /* all done */ });

Return Value:
  An :green:`Object` with a single method, ``finally(fn)``, which
  registers a :green:`Function` to be called once all URLs have
  completed.

When the transpiler is active (``rampart -t``, ``"use transpiler"``
or ``"use transpilerGlobally"``), ``fetchAsync()`` returns a
:green:`Promise` instead:

* With a single ``url`` and no callback, the Promise resolves to the
  `Fetch Results`_ :green:`Object`, or rejects with an
  :green:`Error` if ``errMsg`` is set.

* With a callback, the callback is still called once per URL and
  the Promise resolves when all URLs have completed.

.. code-block:: javascript

    "use transpiler"
    rampart.globalize(rampart.utils);
    var chrome = require("rampart-chromeview");

    async function main() {
        var browser = chrome.launch();
        try {
            var res = await browser.fetchAsync("https://example.com/");
            printf("%d %d\n", res.status, res.text.length);
        } catch(e) {
            printf("error: %s\n", e.message);
        }
        browser.close();
    }
    main();

browser.fetchClose()
""""""""""""""""""""

Usage:

.. code-block:: javascript

    browser.fetchClose([callback]);

Closes the tabs used by ``fetch()`` and ``fetchAsync()`` and
discards their cookies, storage and cache.  The next fetch starts
with a clean browser context.  Throws (or passes an error to the
callback) if fetches are still in progress.  Supports the three
calling conventions described above.

browser.download()
""""""""""""""""""

Save a URL to a local file.  Unlike ``fetch()``, the response never
passes through Rampart: Chrome writes the file itself, so memory stays
flat no matter how large the file is and there is no size limit.  Use
this whenever the file itself is what is wanted; use ``fetch()`` when
the body or the response headers are needed in the script.

Usage:

.. code-block:: javascript

    var res = browser.download(url, path[, options]);

Where:

* ``url`` is a :green:`String`, the URL to save.

* ``path`` is a :green:`String`, the local file to write.  A relative
  path is relative to the script's current directory.  The data is
  written under a temporary name in the same directory and renamed into
  place only when the download succeeds, so ``path`` only ever holds a
  complete file.  If the download fails for any reason -- an HTTP error,
  a network error, a stall, ``maxTime`` -- ``path`` is left exactly as
  it was: an existing file is kept, and no file is created.

* ``options`` is an optional :green:`Object` accepting ``timeout``,
  ``maxTime``, ``headers``, ``userAgent`` and ``referrer``, which mean
  the same as in `Fetch Options`_, and:

  * ``native`` - a :green:`Boolean`.  Set ``false`` to fetch the body
    to the file instead of letting Chrome save it.  Slower and limited
    by memory as described under `Large responses`_, but it relies on
    nothing from Chrome's download handling.  Default ``true``.

* Supports the three calling conventions described above, so a
  callback or (with the transpiler) a :green:`Promise` may be used
  instead of blocking.  A blocking call waits as long as the download
  takes; ``timeout`` still applies to a stall.

Return Value:
    An :green:`Object`:

    * ``ok`` - a :green:`Boolean`, ``true`` if the file was saved.

    * ``file`` - a :green:`String`, the path written.

    * ``bodySize`` - a :green:`Number`, bytes received.

    * ``status`` - a :green:`Number`, the HTTP status, when known.

    * ``totalTime`` - a :green:`Number`, seconds taken.

    * ``errMsg`` - a :green:`String`, present only when ``ok`` is
      ``false``: an HTTP error (``"HTTP 404 Not Found"``), a network
      error (``"net::ERR_NAME_NOT_RESOLVED"``), a stall
      (``"no progress for 30000 ms"``), a ``maxTime`` cut-off, or
      ``"browser closed"`` if the browser was closed first.

.. code-block:: javascript

    var res = browser.download("https://example.com/big.iso",
                               "/var/tmp/big.iso");
    if (!res.ok)
        printf("download failed: %s\n", res.errMsg);
    else
        printf("saved %d bytes in %.1f seconds\n", res.bodySize, res.totalTime);

Content that Chrome would display rather than download -- an HTML page,
plain text, JSON, an image -- is still saved: the response is fetched to
the file instead.  Each download runs in its own private browser
context, so its cookies are not shared with pages or with ``fetch()``.

Fetch Options
"""""""""""""

* ``timeout`` - a :green:`Number`, milliseconds a fetch may make **no
  progress** before it gives up.  Default ``30000``.  It is not a limit
  on how long a transfer may take: the clock restarts whenever more of
  the response arrives, so a large download over a slow link still
  completes while a stalled one fails promptly.  On timeout ``errMsg``
  is set, and anything already received is returned and marked (see
  ``bodyPartial`` and ``filePartial`` under `Fetch Results`_).

* ``maxTime`` - a :green:`Number`, seconds, a hard limit on the whole
  fetch, as in rampart-curl.  Use it when a fetch must return within a
  fixed time no matter what.  Not set by default.

* ``waitUntil`` - a :green:`String`, when an HTML page counts as
  finished:

  * ``"load"`` - the page's ``load`` event (default).
  * ``"domcontentloaded"`` - the HTML has been parsed.
  * ``"networkidle0"`` - no network activity for 500 ms.
  * ``"networkidle2"`` - no more than two requests active for
    500 ms.

  Use a ``networkidle`` setting for pages that fill in their
  content with later requests.

* ``render`` - a :green:`Boolean`.  Set ``false`` to return HTML
  pages without rendering them.  ``text`` is then the raw HTML.
  Default ``true``.

* ``returnText`` - a :green:`Boolean`.  Set ``false`` to omit
  ``text`` for content that was not rendered, saving a copy of large
  bodies.  Default ``true``.

* ``location`` - a :green:`Boolean`.  Whether to follow HTTP
  redirects.  Default ``true``.  When ``false``, the redirect
  response itself is returned; its ``body`` is always empty.

* ``headers`` - an :green:`Object` (``{"X-Name": "value"}``) or an
  :green:`Array` of :green:`String`\ s (``["X-Name: value"]``).
  Extra request headers.

* ``userAgent`` - a :green:`String`, the User-Agent to send.

* ``referrer`` - a :green:`String`, the referring URL.

* ``fresh`` - a :green:`Boolean`.  When ``true``, the fetch runs in
  a new, empty browser context that is discarded afterwards: no
  cookies, storage or cache from earlier fetches, and none left
  behind.  Slower, since a new tab is opened each time.

* ``concurrency`` - a :green:`Number`, how many URLs from one call
  are fetched at the same time.  Default ``4``.

* ``toFile`` - a :green:`String`, a path to write the response body to.
  The bytes go straight to disk as they arrive, so the body never has to
  fit in memory.  The result then has ``file`` and ``bodySize`` instead
  of ``body`` and ``text``.  An existing file is overwritten.  Use it
  when the response headers are wanted along with the file; for a plain
  download `browser.download()`_ is faster and uses far less memory.

* ``chunkSize`` - a :green:`Number`, the read size in bytes used when a
  body is streamed.  Default 8388608 (8 MB).

* ``post`` - a :green:`String`, :green:`Buffer` or :green:`Object`.
  Send the request as a POST with this body.  An :green:`Object` is
  converted with :ref:`rampart-utils:objectToQuery`.  A
  :green:`String` starting with ``@`` is read from that file (start it
  with ``\@`` to send a literal ``@``).  Sent with
  ``Content-Type: application/x-www-form-urlencoded`` unless
  ``headers`` sets a different ``Content-Type``.

* ``postJSON`` - a :green:`String`, :green:`Buffer`, :green:`Object`
  or :green:`Array`.  As ``post``, except that an :green:`Object` or
  :green:`Array` is sent as JSON and the ``Content-Type`` is
  ``application/json``.

* ``postform`` - an :green:`Object`.  Send a
  ``multipart/form-data`` POST, one part per property, as in
  rampart-curl.  Each value may be:

  * a :green:`String` or :green:`Buffer`, sent as is.  A
    :green:`String` starting with ``@`` uploads that file, with its
    base name as the filename.

  * an :green:`Object` with a ``data`` property (as above, or an
    :green:`Object`/:green:`Array` sent as JSON) and optional
    ``filename`` and ``type`` (the part's ``Content-Type``).

  * any other :green:`Object`, sent as JSON.

  * an :green:`Array` of :green:`Object`\ s with ``data``, sending
    several parts with the same name.

  For example:

  .. code-block:: javascript

      var res = browser.fetch("https://example.com/upload", {
          postform: {
              title:  "My photo",
              photo:  "@/path/to/pic.jpg",
              notes:  {data: "some text", filename: "notes.txt", type: "text/plain"}
          }
      });

  Only one of ``post``, ``postJSON`` and ``postform`` is used; if
  several are given, ``postform`` takes precedence, then
  ``postJSON``.

* Redirects after a POST are followed as a browser follows them: a
  ``301``, ``302`` or ``303`` redirect becomes a GET, while ``307``
  and ``308`` repeat the POST with the same body.

Fetch Results
"""""""""""""

* ``body`` - a :green:`Buffer`, the response body exactly as sent by
  the server.

* ``text`` - a :green:`String`.  For a rendered HTML page, the
  rendered document (``document.documentElement.outerHTML``).
  Otherwise the body converted to a string (omitted if
  ``returnText`` is ``false``).

* ``rendered`` - a :green:`Boolean`, ``true`` if ``text`` is a
  rendered page.

* ``status`` - a :green:`Number`, the HTTP status code, or ``0`` if
  no response was received.

* ``statusText`` - a :green:`String`, e.g. ``"OK"`` or
  ``"Not Found"``.

* ``url`` - a :green:`String`, the URL as requested.

* ``effectiveUrl`` - a :green:`String`, the final URL after any
  redirects.

* ``headers`` - an :green:`Object`, the response headers.

* ``rawHeader`` - a :green:`String`, the raw header text, when
  available (not for HTTP/2 or HTTP/3).

* ``totalTime`` - a :green:`Number`, seconds taken.

* ``serverIP``, ``serverPort``, ``httpVersion`` - the server's
  address, port and HTTP version.  Present for rendered pages only.

* ``file`` - a :green:`String`, present only with the ``toFile``
  option: the path the body was written to.

* ``bodySize`` - a :green:`Number`, the number of bytes received.
  Present when the body was streamed or written with ``toFile``.

* ``bodyPartial`` - a :green:`Boolean`, present only when the fetch
  ended early and ``body`` holds part of the response.  ``bodySize``
  gives the number of bytes received.  Always check ``errMsg`` (or
  this) before treating a large body as complete.

* ``filePartial`` - a :green:`Boolean`, the same for a ``toFile``
  download: the file exists but is short.

* ``bodyOmitted`` - a :green:`Boolean`, present only when an HTML page
  was larger than the streaming threshold.  The page is still rendered
  into ``text``, but ``body`` is empty (see `Large responses`_).

* ``errMsg`` - a :green:`String`, present only when the fetch
  failed, e.g. ``"net::ERR_NAME_NOT_RESOLVED"``,
  ``"no progress for 30000 ms"``, or ``"browser closed"`` when
  ``browser.close()`` is called while a ``fetchAsync()`` is still
  running (every unfinished fetch and download is answered this way, so
  callbacks and ``finally()`` always run).  As with rampart-curl, a failed
  fetch sets ``errMsg`` rather than throwing.

Large responses
"""""""""""""""

Bodies up to 64 MB are fetched in a single step.  Anything larger, or
any response that does not say how big it is, is read from Chrome in
chunks instead.  This is not merely an optimization: Chrome stops
answering a request for a whole body once its reply would reach 256 MB,
which is a body of 192 MB, and it gives no error when it does — so
without chunked reads such a fetch would simply never finish.

With ``toFile`` the chunks are written to disk as they arrive and
Rampart's memory stays flat regardless of size.  Without it, the
assembled body is held in memory twice while it is handed back, so
allow roughly twice the file size.

Chrome itself is the real limit for ``fetch()``.  Because it takes the
body from Chrome rather than letting it save the file, Chrome holds the
whole response first: fetching a 1 GB file was measured at about 7.5 GB
of Chrome memory.  A few hundred megabytes is comfortable and a gigabyte
works, but there is no reason to pay that when only the file is wanted:
`browser.download()`_ lets Chrome write it instead.  The same 1 GB file
downloads in a quarter of the time with about 1 GB of Chrome memory and
16 MB in Rampart, and has no size limit at all.

An HTML page is a special case: Chrome cannot both hand over the body
and render it, so a page larger than 64 MB is rendered into ``text``
with ``body`` left empty and ``bodyOmitted`` set.  Pass
``render: false`` to get such a page's bytes instead of its rendering.

Differences from rampart-curl:

* Only GET and POST requests are supported.

* rampart-curl's ``post301``, ``post302`` and ``post303`` options
  have no equivalent; redirects after a POST are handled as
  described under `Fetch Options`_ above.

* ``localIP``, ``localPort`` and ``cookies`` are not provided.

* The body of a redirect response is not available.

* A page that navigates itself elsewhere with JavaScript is not
  followed; the result is the page as originally requested.

* Chrome retrieves simultaneous requests for the same URL one at a
  time.

Example:

.. code-block:: javascript

    rampart.globalize(rampart.utils);
    var chrome = require("rampart-chromeview");
    var browser = chrome.launch();

    /* a page built by JavaScript */
    var res = browser.fetch("https://example.com/app",
                            {waitUntil: "networkidle0"});
    printf("%d %s\n", res.status, res.statusText);
    printf("%s\n", res.text);

    /* several urls, including a binary file */
    browser.fetch([
        "https://example.com/",
        "https://example.com/files/archive.tar.gz"
    ], function(res) {
        if (res.errMsg)
            printf("%s: %s\n", res.url, res.errMsg);
        else
            printf("%s: %d bytes\n", res.url, res.body.length);
    });

    browser.close();

BrowserContext
--------------

An isolated browser context — analogous to a profile, with its own
cookie jar and storage.  Returned from
``browser.createBrowserContext()`` and
``browser.defaultBrowserContext()``.

* ``context.pages([cb])`` - The pages belonging to this context.

* ``context.targets([cb])`` - ``browser.targets()`` filtered to
  this context.

* ``context.isIncognito()`` - ``true`` for contexts created via
  ``createBrowserContext``; ``false`` for the default context.

* ``context.newPage([cb])`` - Open a new tab **inside this
  context**.  Cookies set here do not leak into the default
  context (or vice versa).

* ``context.close([cb])`` - Dispose the context (closes all its
  pages, deletes its cookies).  A no-op on the default context.

* ``context.overridePermissions(origin, perms [, cb])`` -
  Pre-grant permissions to ``origin`` so the page is not prompted.
  ``perms`` is an array of names from `Chrome's permission set
  <https://chromedevtools.github.io/devtools-protocol/tot/Browser/#type-PermissionType>`_:
  ``"geolocation"``, ``"notifications"``, ``"camera"``,
  ``"microphone"``, ``"clipboardReadWrite"``,
  ``"backgroundSync"``, etc.

* ``context.clearPermissionOverrides([cb])`` - Reset any prior
  overrides.

Page
----

A single browser tab.  ``Page`` carries the bulk of the
rampart-chromeview API and is returned from
``browser.newPage()``, ``context.newPage()``,
``browser.pages()``, and other entry points.

Navigation
~~~~~~~~~~

* ``page.goto(url [, options] [, cb])`` - Navigate to ``url``.
  ``options.waitUntil`` is ``"load"`` (default) or
  ``"domcontentloaded"``.  ``options.referrer`` is an optional
  :green:`String`.

* ``page.reload([cb])`` - Reload the current page, waiting for
  ``load``.

* ``page.goBack([cb])``, ``page.goForward([cb])`` - Navigate one
  step backward / forward.  Resolves to ``null`` if there is no
  entry in that direction.

* ``page.close([cb])`` - Close this tab.

Content and Evaluation
~~~~~~~~~~~~~~~~~~~~~~

* ``page.evaluate(jsString [, cb])`` - Evaluate a JavaScript
  expression string in the page's main execution context.  If the
  expression evaluates to a Promise, it is awaited (via CDP's
  ``awaitPromise: true``).  Thrown exceptions in the page become
  thrown Errors in Rampart.

  .. code-block:: javascript

      var n = page.evaluate("40 + 2");                       // 42
      var o = page.evaluate("({ok:true, v:document.title})");
      var d = page.evaluate("fetch('/x').then(r=>r.json())"); // awaited

* ``page.evaluateHandle(jsStringOrFn [, args...] [, cb])`` - Like
  ``evaluate``, but returns a `JSHandle`_ (or `ElementHandle`_ for
  DOM nodes) wrapping the in-page result instead of serializing
  it.  This is the appropriate form for values that do not survive
  JSON serialization (``window``, ``Map``, function objects, and
  similar).

* ``page.content([cb])`` - Returns the current page's HTML
  (equivalent to ``document.documentElement.outerHTML``).

* ``page.setContent(html [, cb])`` - Replace the page's current
  document with ``html``.

* ``page.title([cb])``, ``page.url([cb])`` - Return
  ``document.title`` and ``location.href`` respectively.

* ``page.evaluateOnNewDocument(fnOrSrc [, args...] [, cb])`` -
  Register a script (string or function) to run before any of the
  page's own scripts on every subsequent document load (including
  iframes).  Returns the CDP identifier string.

* ``page.addScriptTag(options [, cb])`` - Inject a ``<script>``
  into the current document.  ``options`` is one of ``{url}``,
  ``{path}``, or ``{content}`` (and optionally ``{type}``).

* ``page.addStyleTag(options [, cb])`` - Same shape as
  ``addScriptTag``, but injects a ``<link rel=stylesheet>`` (for
  ``{url}``) or ``<style>`` (for ``{content}``/``{path}``).

Selectors
~~~~~~~~~

* ``page.$(selector [, cb])`` - ``document.querySelector``,
  returning an `ElementHandle`_ or ``null``.

* ``page.$$(selector [, cb])`` - ``document.querySelectorAll`` as
  an :green:`Array` of `ElementHandle`_\ s.

* ``page.$eval(selector, pageFnString [, args...] [, cb])`` - Run
  ``pageFnString`` in the page with the matched element as the
  first argument.  Throws if the selector matches nothing.

  .. code-block:: javascript

      var text = page.$eval("h1", "el => el.textContent");
      var wide = page.$eval("#box",
          "(el, padding) => el.offsetWidth + padding", 10);

* ``page.$$eval(selector, pageFnString [, args...] [, cb])`` -
  Same, but ``pageFnString`` gets an array of all matching
  elements.

* ``page.$x(xpath [, cb])`` - XPath version of ``$$``.

Interaction
~~~~~~~~~~~

* ``page.click(selector [, options] [, cb])`` - Resolve the
  element, scroll it into view, and dispatch real mouse events at
  its bounding-box centre.  ``options.button`` is ``"left"``
  (default), ``"right"``, or ``"middle"``.
  ``options.clickCount`` (default ``1``).  ``options.delay``
  milliseconds between mousedown and mouseup.  ``options.jsClick``
  is a :green:`Boolean` — when ``true``, mouse dispatch is skipped
  and ``element.click()`` is invoked via JavaScript instead.  This
  alternative path is needed for elements with no bounding box
  (for example, ``display:none``).

* ``page.focus(selector [, cb])``,
  ``page.hover(selector [, cb])`` - Focus or hover the matched
  element.

* ``page.type(selector, text [, cb])`` - Focus the matched
  input/textarea and set its value.  Dispatches ``input`` and
  ``change`` events but not individual keydown events; for
  realistic typing, use `Keyboard`_ ``type``.

* ``page.select(selector, value1 [, value2, ...] [, cb])`` -
  Select option(s) in a ``<select>`` element by ``value``.  Fires
  ``input`` and ``change`` events.  Returns an :green:`Array` of
  the values that ended up selected.

Waiting
~~~~~~~

* ``page.waitForSelector(selector [, options] [, cb])`` - Poll
  in-page until ``document.querySelector(selector)`` matches, or
  the timeout elapses.  ``options.timeout`` defaults to
  ``30000`` ms (``0`` waits indefinitely).
  ``options.polling`` is the interval between checks (default
  ``50`` ms).

* ``page.waitForFunction(pageFnString [, options] [, cb])`` -
  Poll ``pageFnString`` in the page until it returns a truthy
  value.  The truthy value is returned.

* ``page.waitForNavigation([options] [, cb])`` - Block until the
  next ``load`` (or ``domcontentloaded``) event on this page.
  **Install the waiter BEFORE triggering the navigation** —
  there is a small race window otherwise:

  .. code-block:: javascript

      /* Transpiled mode — idiomatic */
      const navP = page.waitForNavigation();
      page.click("a.submit");
      await navP;

* ``page.waitForRequest(urlMatch [, options] [, cb])`` - Mirror
  of ``waitForResponse`` on the request side.  ``urlMatch`` is a
  substring or a slash-wrapped regex source.

* ``page.waitForResponse(urlMatch [, options] [, cb])`` - Block
  until a network response arrives whose URL matches.  Returns a
  `Response`_.

* ``page.waitForXPath(xpath [, options] [, cb])`` - Poll until
  ``document.evaluate(xpath, ...)`` matches a node.

* ``page.waitForTimeout(ms [, cb])`` - Pause for ``ms``
  milliseconds in-page.

* ``page.waitForFileChooser([options] [, cb])`` - Arm Chrome to
  intercept the next file chooser dialog (instead of showing a
  native picker) and resolve to a `FileChooser`_:

  .. code-block:: javascript

      var chooserP = page.waitForFileChooser();
      page.click("#upload-button");                 // user gesture
      var chooser  = await chooserP;
      chooser.accept(["/path/to/file.csv"]);

Screenshots and PDF
~~~~~~~~~~~~~~~~~~~

* ``page.screenshot([options] [, cb])`` - Returns a PNG Buffer by
  default.  ``options``:

  * ``type`` - ``"png"`` (default) or ``"jpeg"``

  * ``quality`` - JPEG quality, 0–100

  * ``fullPage`` - a :green:`Boolean`; if ``true``, capture the
    entire document

  * ``clip`` - ``{x, y, width, height, scale}``

  * ``encoding`` - ``"base64"`` to return a string instead of a
    Buffer

* ``page.pdf([options] [, cb])`` - Returns a PDF Buffer.
  **Headless mode only.**

  ``options`` (inches): ``format`` (``"letter"`` default,
  ``"legal"``, ``"tabloid"``, ``"a3"``, ``"a4"``, ``"a5"``),
  ``landscape``, ``printBackground``, ``scale``, ``paperWidth``,
  ``paperHeight``, ``marginTop``, ``marginBottom``,
  ``marginLeft``, ``marginRight``, ``preferCSSPageSize``.

Cookies
~~~~~~~

* ``page.setCookie(cookie [, cookie2, ...] [, cb])`` - Set one or
  more cookies.  Each cookie is an :green:`Object`:
  ``{name, value, domain, path, expires?, httpOnly?, secure?,
  sameSite?}``.

* ``page.cookies([url1, url2, ...] [, cb])`` - Returns cookies.
  With no URLs, returns all cookies in the current context.
  Otherwise, returns cookies visible to the given URLs.

* ``page.deleteCookie(spec [, spec2, ...] [, cb])`` - Delete
  cookies matching each spec ``{name, url?, domain?, path?}``.

Emulation
~~~~~~~~~

* ``page.setViewport({width, height, deviceScaleFactor?,
  isMobile?, hasTouch?} [, cb])`` - Override device metrics.

* ``page.viewport()`` - Return the last viewport set, or query
  the live values from the page when nothing has been set.

* ``page.emulate()``

  .. _page.emulate():

  ``page.emulate(deviceOrName [, cb])`` - Apply both viewport and
  user-agent in one call.  Accepts a device name (string) — looked
  up in `chrome.devices`_ — or a descriptor.

  .. code-block:: javascript

      page.emulate("iPhone 12");
      page.goto("https://example.com/");

* ``page.emulateMediaType(type [, cb])`` - ``type`` is
  ``"screen"``, ``"print"``, or ``null`` to reset.  Affects
  ``@media`` queries, which is useful when rendering print
  stylesheets via ``page.pdf``.

* ``page.setGeolocation({latitude, longitude, accuracy?} [, cb])``
  - Spoof the values returned by
  ``navigator.geolocation.getCurrentPosition``.  Geolocation
  permission still needs to be granted to the origin.

* ``page.setUserAgent(userAgent [, cb])`` - Override
  ``navigator.userAgent`` and the ``User-Agent`` header on
  requests.

* ``page.setExtraHTTPHeaders(headers [, cb])`` - ``headers`` is
  ``{name: value, ...}``.  Applied to all subsequent requests
  initiated by this page.

* ``page.setJavaScriptEnabled(enabled [, cb])`` - Disable
  JavaScript execution for subsequent navigations.

* ``page.setDragInterception(enabled [, cb])`` - When ``true``,
  Chrome forwards drag operations to the host as
  ``Input.dragIntercepted`` events instead of running its own
  drag UI.  Prerequisite for HTML5 drag-and-drop via
  ``page.mouse.drag``/``dragEnter``/``dragOver``/``drop``.

* ``page.setBypassCSP(enabled [, cb])`` - Bypass any
  Content-Security-Policy headers the page returns.

* ``page.setCacheEnabled(enabled [, cb])`` - Toggle Chrome's HTTP
  cache for this page.

* ``page.setOfflineMode(offline [, cb])`` - Simulate an offline
  network condition.

* ``page.emulateMediaFeatures(features [, cb])`` - Override
  ``@media (feature: value)`` queries.  ``features`` is an array
  of ``{name, value}`` objects such as
  ``[{name: "prefers-color-scheme", value: "dark"}]``.

* ``page.emulateTimezone(tz [, cb])`` - Override the page's
  timezone.  ``tz`` is an IANA name (e.g.
  ``"America/Los_Angeles"``).

* ``page.emulateCPUThrottling(rate [, cb])`` - Throttle CPU by
  ``rate`` (``1`` = no throttling, ``2`` = 2x slower, etc.).

* ``page.emulateNetworkConditions(conditions [, cb])``

  .. _page.emulateNetworkConditions():

  ``conditions`` is either a preset name from
  `chrome.networkConditions`_ (e.g. ``"Slow 3G"``) or an object
  ``{offline, latency, downloadThroughput, uploadThroughput}``.
  Pass ``null`` to clear.

* ``page.emulateVisionDeficiency(type [, cb])`` - ``type`` is one
  of ``"none"``, ``"achromatopsia"``, ``"blurredVision"``,
  ``"deuteranopia"``, ``"protanopia"``, ``"tritanopia"``,
  ``"reducedContrast"``.

Misc page accessors
~~~~~~~~~~~~~~~~~~~

* ``page.metrics([cb])`` - Return a snapshot of Chrome's
  ``Performance.getMetrics`` as a flat ``{name: number}`` object.

* ``page.isClosed()`` - Returns ``true`` once ``page.close()`` has
  been called.

* ``page.bringToFront([cb])`` - Bring this page's tab to the
  foreground.  No-op in headless mode.

* ``page.target()``, ``page.browser()``,
  ``page.browserContext()`` - Back-references.

* ``page.workers()`` - The array of `Worker`_ instances attached
  to this page.  Arms ``Target.setAutoAttach`` so future workers
  also appear.

* ``page.queryObjects(prototypeHandle [, cb])`` - Returns a
  `JSHandle`_ for the array of all live objects in the page whose
  prototype matches.

* ``page.setDefaultTimeout(ms)`` /
  ``page.setDefaultNavigationTimeout(ms)`` - Change the default
  timeout used by ``waitForSelector``, ``waitForFunction``,
  ``waitForXPath``, etc.  Per-call ``options.timeout`` still
  wins.  Returns the page for chaining.

* ``page.authenticate({username, password} [, cb])`` - Provide
  HTTP Basic/Digest credentials.  Chrome re-runs any request that
  gets a 401/407 challenge using the supplied credentials.  Pass
  ``null`` to clear.

Page events
~~~~~~~~~~~

``page.on(event, handler)``, ``page.off(event, handler)``,
``page.once(event, handler)``,
``page.removeListener(event, handler)``.

================================  ================================================
Event                             Handler args
================================  ================================================
``"load"``                        ``()``
``"domcontentloaded"``            ``()``
``"console"``                     ``(ConsoleMessage)`` - see `ConsoleMessage`_
``"pageerror"``                   ``(Error)``
``"dialog"``                      ``(Dialog)`` - **must** call ``.accept()`` /
                                  ``.dismiss()``; see `Dialog`_
``"framenavigated"``              ``(frame)`` - raw CDP frame object
``"request"``                     ``(Request)`` - requires
                                  ``setRequestInterception(true)``
``"response"``                    ``(Response)``
``"requestfailed"``               ``(Request)``
``"requestfinished"``             ``(Request)``
``"requestservedfromcache"``      ``({requestId})``
``"close"``                       ``()``
``"popup"``                       ``(Page)``
``"frameattached"``               ``({frameId, parentFrameId?})``
``"framedetached"``               ``({frameId, reason?})``
``"error"``                       ``(payload)`` - ``Inspector.targetCrashed``
``"workercreated"``               ``(Worker)``
``"workerdestroyed"``             ``(Worker)``
================================  ================================================

Handler functions are invoked on the main thread's event loop and
will not fire during a sequence of blocking sync calls; see
`Event-driven features need the main event loop`_.

Binding functions
~~~~~~~~~~~~~~~~~

* ``page.exposeFunction(name, fn [, cb])`` - Make the Rampart
  function ``fn`` callable from page JavaScript as
  ``window.<name>(...args)``.  The page-side call returns a
  Promise resolving to ``fn``'s return value (or rejecting with a
  thrown error).

  .. code-block:: javascript

      page.exposeFunction("lookupUser", function(id) {
          return sql.one("SELECT * FROM users WHERE id=?", [id]);
      });

  In the page:

  .. code-block:: javascript

      const user = await window.lookupUser(42);

  ``fn`` runs on the main thread's event loop, so
  ``exposeFunction`` only works once the main script has yielded
  to the loop.

* ``page.removeExposedFunction(name [, cb])`` - Inverse of
  ``exposeFunction``.

Request interception
~~~~~~~~~~~~~~~~~~~~

* ``page.setRequestInterception(enabled [, cb])`` - When
  ``true``, subsequent requests pause and fire
  ``page.on("request", ...)``.  The registered handler **must**
  call ``req.continue()``, ``req.abort()``, or ``req.respond()``
  on every request — otherwise the page hangs.

  .. code-block:: javascript

      page.setRequestInterception(true);
      page.on("request", function(req) {
          if (/ads/.test(req.url())) { req.abort("BlockedByClient"); return; }
          req.continue();
      });

Frame
-----

Iframes (especially cross-origin ones) have their own JavaScript
execution context and are invisible to ``page.evaluate`` /
``page.$`` in the main frame.  Use ``page.frames()`` to enumerate
and reach inside them.

* ``page.frames([cb])`` - Return an :green:`Array` of `Frame`_
  objects, starting with the main frame followed by its
  descendants (depth-first).

* ``page.mainFrame([cb])`` - Return the top-level `Frame`_.

Frames have the same DOM-facing methods as Page, scoped to the
frame's execution context:

* ``frame.evaluate(jsString [, cb])``

* ``frame.evaluateHandle(jsStringOrFn [, args...] [, cb])``

* ``frame.goto(url [, options] [, cb])``,
  ``frame.setContent(html [, cb])``,
  ``frame.waitForNavigation([options] [, cb])``

* ``frame.addScriptTag({url|path|content} [, cb])``,
  ``frame.addStyleTag({url|path|content} [, cb])``

* ``frame.$(selector [, cb])``, ``frame.$$(selector [, cb])``,
  ``frame.$x(xpath [, cb])``

* ``frame.$eval(selector, pageFnString [, args...] [, cb])``,
  ``frame.$$eval(selector, pageFnString [, args...] [, cb])``

* ``frame.click(selector [, cb])``,
  ``frame.type(selector, text [, cb])``,
  ``frame.focus(selector [, cb])``,
  ``frame.hover(selector [, cb])`` - like the ``page.*``
  versions, but use synthetic DOM events (mouse coordinates would
  be relative to the iframe rather than the viewport).

* ``frame.select(selector, value1, ... [, cb])``

* ``frame.waitForSelector(selector [, options] [, cb])``,
  ``frame.waitForFunction(pageFnString [, options] [, cb])``,
  ``frame.waitForXPath(xpath [, options] [, cb])``,
  ``frame.waitForTimeout(ms [, cb])``

* ``frame.waitForRequest(urlMatch [, options] [, cb])``,
  ``frame.waitForResponse(urlMatch [, options] [, cb])``

* ``frame.content([cb])``, ``frame.title([cb])``

* ``frame.url()``, ``frame.name()``, ``frame.frameId()``

* ``frame.parentFrame()``, ``frame.childFrames()``,
  ``frame.isDetached()``

* ``frame.page()``

JSHandle
--------

A handle to any in-page JavaScript value that cannot (or should
not) be serialized over JSON.  Returned by
``page.evaluateHandle``, ``frame.evaluateHandle``,
``JSHandle.getProperty``, etc.

* ``handle.evaluate(fnSrc [, args...] [, cb])`` - Run ``fnSrc``
  with this handle bound as the first argument; serializes the
  return value.

* ``handle.evaluateHandle(fnSrc [, args...] [, cb])`` - Same, but
  returns another handle instead of serializing.

* ``handle.jsonValue([cb])`` - Best-effort serialization of the
  value.

* ``handle.getProperty(name [, cb])`` - Returns a JSHandle for
  ``obj[name]``.

* ``handle.getProperties([cb])`` - Returns ``{name: JSHandle}``
  of own keys.

* ``handle.asElement()`` - Returns ``this`` if it is an
  `ElementHandle`_, else ``null``.

* ``handle.dispose([cb])`` - Release the remote-object reference
  (idempotent).

`ElementHandle`_ is a subclass — every `ElementHandle`_ is a
`JSHandle`_, but only DOM nodes get the DOM-specific helpers
below.

ElementHandle
-------------

Returned from ``page.$()``, ``page.$$()``, ``page.$x()``,
``frame.$()``, ``frame.$$()``, and from ``evaluateHandle`` when
the value is a DOM node.  Backed by a remote-object reference in
the page; the reference is released by ``.dispose()``.

* ``handle.evaluate(fnSrcString [, args...] [, cb])`` - Runs with
  ``this`` (and first argument) bound to the element.

* ``handle.textContent([cb])``,
  ``handle.getAttribute(name [, cb])``

* ``handle.boundingBox([cb])`` - Returns
  ``{x, y, width, height}`` or ``null``.

* ``handle.screenshot([options] [, cb])`` - Page screenshot
  clipped to this element's bounding box.

* ``handle.click([cb])``, ``handle.focus([cb])``,
  ``handle.hover([cb])``

* ``handle.type(text [, cb])`` - Sets value and fires
  ``input``/``change``.

* ``handle.press(key [, opts] [, cb])`` - Focus the element, then
  dispatch a real key press.

* ``handle.tap([cb])`` - Touch-emulated tap via
  ``Input.dispatchTouchEvent``.

* ``handle.select(value1 [, value2, ...] [, cb])`` - Set a
  ``<select>`` to the listed option values; returns the array of
  values that ended up selected.

* ``handle.uploadFile(path1 [, path2, ...] [, cb])`` - Set the
  file list on an ``<input type=file>`` element.

* ``handle.contentFrame([cb])`` - For an ``<iframe>``/
  ``<frame>`` element, returns the `Frame`_ inside it, or
  ``null``.

* ``handle.scrollIntoView([cb])`` - Scroll the element into the
  middle of the viewport.

* ``handle.isVisible([cb])`` / ``handle.isHidden([cb])`` -
  Display and size check.

* ``handle.isIntersectingViewport([opts] [, cb])`` - ``true`` if
  any part is in the viewport (``opts.threshold`` 0–1).

* ``handle.$(sel [, cb])``, ``handle.$$(sel [, cb])``,
  ``handle.$x(xp [, cb])`` - Scoped descendants.

* ``handle.$eval(sel, fnStr [, args...] [, cb])``,
  ``handle.$$eval(sel, fnStr [, args...] [, cb])``

* ``handle.drag(targetHandleOrPoint [, cb])`` - Start an HTML5
  drag from this element's centre; returns the captured
  ``DragData``.

* ``handle.dragEnter(data [, cb])``,
  ``handle.dragOver(data [, cb])``,
  ``handle.drop(data [, cb])`` - Dispatch the HTML5 drag event at
  this element's centre.

* ``handle.dragAndDrop(targetHandleOrPoint [, opts] [, cb])`` -
  Full HTML5 drag-and-drop from this element to the target.

* ``handle.dispose([cb])`` - Release the remote-object reference.

.. code-block:: javascript

    var h = page.$("#username");
    h.type("alice");
    printf("value: %s\n", h.evaluate("el => el.value"));
    h.dispose();

    /* Screenshot a specific card */
    var card = page.$(".product-card");
    fprintf("/tmp/card.png", "%s", card.screenshot());
    card.dispose();

Mouse
-----

Accessed as ``page.mouse``.  Dispatches real CDP
``Input.dispatchMouseEvent``\ s.

* ``page.mouse.move(x, y [, opts] [, cb])``

* ``page.mouse.down([opts] [, cb])`` -
  ``{button, clickCount}``

* ``page.mouse.up([opts] [, cb])``

* ``page.mouse.click(x, y [, opts] [, cb])`` -
  ``{button, clickCount, delay}``.  Move, press, wait, release.

* ``page.mouse.wheel([opts] [, cb])`` - ``{deltaX, deltaY}``

* ``page.mouse.dragAndDrop({x, y}, {x, y} [, opts] [, cb])`` -
  Drag-and-drop.  When
  ``page.setDragInterception(true)`` has been called, dispatches
  HTML5 drag events; otherwise falls back to ``clickAndDrag``.
  ``opts``: ``{steps, delay}``.

* ``page.mouse.clickAndDrag({x, y}, {x, y} [, opts] [, cb])`` -
  Mouse-event click-and-drag for UIs that do not use HTML5 drag.

* ``page.mouse.drag({x, y}, {x, y} [, cb])`` - Start an HTML5
  drag and return the captured ``DragData``.  Requires
  ``page.setDragInterception(true)``.

* ``page.mouse.dragEnter({x, y}, data [, cb])``,
  ``page.mouse.dragOver({x, y}, data [, cb])``,
  ``page.mouse.drop({x, y}, data [, cb])``

* ``page.mouse.reset([cb])`` - Release any pressed buttons.

Keyboard
--------

Accessed as ``page.keyboard``.  Dispatches real CDP
``Input.dispatchKeyEvent``\ s.

* ``page.keyboard.down(key [, cb])``,
  ``page.keyboard.up(key [, cb])``

* ``page.keyboard.press(key [, opts] [, cb])`` -
  ``opts.delay`` (ms) between down and up.

* ``page.keyboard.type(text [, opts] [, cb])`` - Fast by default
  (uses ``Input.insertText``).  Pass ``{delay: ms}`` to
  interleave a per-character delay.

* ``page.keyboard.sendCharacter(char [, cb])``

* ``page.keyboard.reset([cb])`` - Release any held modifiers.

``key`` is either a single character (``"a"``) or a named key:
``"Enter"``, ``"Tab"``, ``"Backspace"``, ``"Delete"``,
``"Escape"``, ``"ArrowLeft"``, ``"ArrowRight"``, ``"ArrowUp"``,
``"ArrowDown"``, ``"Home"``, ``"End"``, ``"PageUp"``,
``"PageDown"``, ``"Space"``, ``"Shift"``, ``"Control"``,
``"Alt"``, ``"Meta"``.

Request
-------

Passed to ``page.on("request", handler)`` when interception is on,
and to ``page.on("requestfailed"|"requestfinished", handler)`` for
any request (no interception needed).

* ``req.url()``, ``req.method()``, ``req.headers()``,
  ``req.postData()``, ``req.resourceType()``

* ``req.frame()`` - The `Frame`_ that initiated this request.

* ``req.response()`` - The matching `Response`_, once
  ``Network.responseReceived`` has fired (otherwise ``null``).

* ``req.failure()`` - ``{errorText}`` if the request failed,
  else ``null``.

* ``req.redirectChain()`` - Array of `Request`_\ s representing
  the redirect steps that led here.

* ``req.isNavigationRequest()`` - ``true`` for top-level
  ``Document`` requests.

* ``req.initiator()`` - The raw ``Network.Initiator``.

* ``req.continue([overrides] [, cb])`` - Let the request
  proceed, with optional ``{url, method, headers, postData}``
  overrides.

* ``req.abort([reason] [, cb])`` - e.g. ``"BlockedByClient"``,
  ``"Failed"``, ``"ConnectionClosed"``, ``"AccessDenied"``,
  ``"InternetDisconnected"``.

* ``req.respond({status, headers, body} [, cb])`` - Fulfill from
  Rampart without hitting the network.  ``body`` can be a
  :green:`String` or :green:`Buffer`.

Response
--------

Passed to ``page.on("response", handler)`` and returned from
``page.waitForResponse(urlMatch, ...)``.

* ``resp.url()``, ``resp.status()``, ``resp.statusText()``,
  ``resp.headers()``

* ``resp.mimeType()``

* ``resp.ok()`` - ``true`` for status 2xx (or 0 for ``data:``).

* ``resp.request()`` - The originating `Request`_.

* ``resp.frame()`` - The `Frame`_ that initiated the matching
  request.

* ``resp.fromCache()`` - ``true`` if served from disk/prefetch
  cache.

* ``resp.fromServiceWorker()`` - ``true`` if served by a SW.

* ``resp.remoteAddress()`` - ``{ip, port}`` of the server, or
  ``null``.

* ``resp.timing()`` - Raw ``Network.ResourceTiming`` (or
  ``null``).

* ``resp.securityDetails()`` - Raw ``SecurityDetails`` (or
  ``null``).

* ``resp.text([cb])`` - Body decoded to a :green:`String`.

* ``resp.buffer([cb])`` - Body as a raw :green:`Buffer`.

* ``resp.json([cb])`` - ``JSON.parse(resp.text())``.

Target
------

A CDP target: a page, service worker, browser-level target, etc.

* ``target.targetId()``, ``target.type()``, ``target.url()``

* ``target.browser()``, ``target.browserContext()``

* ``target.page()`` - Returns the `Page`_ instance if this is a
  page-type target and the corresponding ``Page`` is already
  tracked; otherwise ``null``.

* ``target.createCDPSession([cb])`` - Attach (if needed) and
  return a `CDPSession`_ for raw CDP commands against this target.

CDPSession
----------

The raw escape hatch for any CDP command not covered by the
higher-level wrappers.  Returned from
``target.createCDPSession()`` (or use the page's existing session
via ``page.target().createCDPSession()``).

* ``session.send(method [, params] [, cb])`` - Issue any CDP
  command.

* ``session.on(method, handler)`` - Subscribe to any CDP event;
  handler receives the raw params object.

* ``session.off(method [, handler])`` - Remove a listener (or all
  listeners for ``method`` if ``handler`` is omitted).

* ``session.detach([cb])`` - Close the session.

* ``session.id()`` - The underlying CDP ``sessionId``.

.. code-block:: javascript

    var session = page.target().createCDPSession();
    session.send("Animation.enable");
    session.on("Animation.animationStarted", function(p) {
        printf("animation started: %s\n", p.animation.id);
    });

Worker
------

Returned from ``page.workers()`` and the ``workercreated`` event.

* ``worker.url()``, ``worker.type()``, ``worker.targetId()``

* ``worker.evaluate(jsStringOrFn [, args...] [, cb])`` - Run
  code in the worker's global scope.

Coverage
--------

Accessed as ``page.coverage``.

* ``page.coverage.startJSCoverage([opts] [, cb])`` -
  ``opts.reportAnonymousScripts: true`` includes anonymous
  scripts.

* ``page.coverage.stopJSCoverage([cb])`` - Returns the array of
  ``Profiler.ScriptCoverage`` objects from
  ``Profiler.takePreciseCoverage``.

* ``page.coverage.startCSSCoverage([cb])``,
  ``page.coverage.stopCSSCoverage([cb])`` - Returns
  ``CSS.RuleUsage[]`` describing which rules ran.

Tracing
-------

Accessed as ``page.tracing``.

* ``page.tracing.start([opts] [, cb])`` - ``opts.categories``
  (string array) and ``opts.screenshots`` (bool).

* ``page.tracing.stop([cb])`` - Returns a :green:`Buffer`
  containing a JSON array of trace events.  Pass ``opts.path`` to
  ``start()`` to also write to disk alongside the returned buffer.

FileChooser
-----------

Returned from ``page.waitForFileChooser()``.  Resolve it before
the user gesture's task finishes, or Chrome will time the picker
out.

* ``chooser.isMultiple()`` - ``true`` if the input has
  ``multiple``.

* ``chooser.accept(paths [, cb])`` - Set the file list; ``paths``
  is a :green:`String` or an :green:`Array` of strings.  Files
  must exist on the Rampart host.

* ``chooser.cancel([cb])`` - Dismiss without selecting files.

Accessibility
-------------

Accessed as ``page.accessibility``.

* ``page.accessibility.snapshot([opts] [, cb])`` - Return a tree
  of ``{role, name, value?, children?}`` mirroring the page's
  accessibility tree.

.. code-block:: javascript

    var snap = page.accessibility.snapshot();
    (function walk(n, d) {
        printf("%s%s%s\n", "  ".repeat(d), n.role,
            n.name ? ' "' + n.name + '"' : "");
        (n.children || []).forEach(function(c) { walk(c, d + 1); });
    })(snap, 0);

ConsoleMessage
--------------

Passed to ``page.on("console", handler)``.

* ``m.type()`` - ``"log"``, ``"warning"``, ``"error"``,
  ``"info"``, ``"debug"``, ...

* ``m.text()`` - Arguments joined into a single string.

* ``m.args()`` - The raw CDP ``RemoteObject``\ s passed to the
  console call.

* ``m.location()`` - ``{url, lineNumber, columnNumber}`` of the
  call site (best-effort).

Dialog
------

Passed to ``page.on("dialog", handler)`` when the page invokes
``alert()``, ``confirm()``, ``prompt()``, or a ``beforeunload``.
**The handler must call ``.accept()`` or ``.dismiss()`` or the
page blocks forever** — ``confirm()`` and ``prompt()`` in
particular do not return until the dialog is resolved.

* ``dialog.type()`` - ``"alert"`` / ``"confirm"`` / ``"prompt"``
  / ``"beforeunload"``.

* ``dialog.message()`` - The text passed to the JS call.

* ``dialog.defaultValue()`` - The default prompt response
  (prompt only).

* ``dialog.accept([promptText] [, cb])`` - Accept; for
  ``prompt``, pass the text to return to the page.

* ``dialog.dismiss([cb])`` - Cancel.

.. code-block:: javascript

    page.on("dialog", async function(d) {
        if (d.type() === "confirm") await d.accept();
        else                        await d.dismiss();
    });

Example: scrape a rendered SPA and save a PDF
---------------------------------------------

.. code-block:: javascript

    "use transpiler"
    rampart.globalize(rampart.utils);
    const chrome = require("rampart-chromeview");

    async function main() {
        const browser = await chrome.launch({headless: true});
        const page    = await browser.newPage();

        /* Block analytics and image requests to speed things up. */
        await page.setRequestInterception(true);
        page.on("request", function(req) {
            const url = req.url();
            if (/google-analytics|doubleclick/.test(url)) return req.abort();
            if (req.resourceType() === "image")           return req.abort();
            req.continue();
        });

        /* Pretend to be a desktop browser. */
        await page.setViewport({width: 1280, height: 800});
        await page.setUserAgent("Mozilla/5.0 rampart-chromeview");

        /* Load the page and wait for the target element. */
        await page.goto("https://example.com/dashboard", {waitUntil: "load"});
        await page.waitForSelector("#report-root", {timeout: 10000});

        /* Pull structured data out of the rendered DOM. */
        const rows = await page.$$eval("#report-root tr",
            "els => els.map(tr => Array.from(tr.children).map(td => td.textContent.trim()))");
        printf("got %d rows\n", rows.length);

        /* Render the same view to a PDF. */
        const pdf = await page.pdf({format: "letter", printBackground: true});
        fprintf("/tmp/report.pdf", "%s", pdf);
        printf("wrote /tmp/report.pdf (%d bytes)\n", pdf.length);

        await browser.close();
    }

    main().catch(function(e) { printf("error: %s\n", e && e.stack || e); });

Using puppeteer-extras
----------------------

Rampart ships an accompanying single-file module,
**puppeteer-extras**, that packages
`puppeteer-extra@3.3.6
<https://www.npmjs.com/package/puppeteer-extra>`_ and
`puppeteer-extra-plugin-stealth@2.11.2
<https://www.npmjs.com/package/puppeteer-extra-plugin-stealth>`_
together with their entire transitive npm closure (37 packages,
141 internal modules) into a single ``require()``\ -able file
that resolves its own internal dependencies.  The module is
intended to be used in conjunction with rampart-chromeview, which
serves as the underlying launcher.

What it provides
~~~~~~~~~~~~~~~~

* ``addExtra(launcher)`` - Wraps a Puppeteer-compatible launcher
  (``rampart-chromeview`` itself) and returns a
  ``PuppeteerExtra`` instance that supports the
  ``puppeteer-extra`` plugin pipeline.

* ``StealthPlugin()`` - The stock
  ``puppeteer-extra-plugin-stealth`` factory.  Returns a plugin
  with 16 stock evasions (``navigator.webdriver``,
  ``navigator.languages``, ``navigator.plugins``, ``chrome.runtime``,
  WebGL vendor/renderer spoofing, user-agent normalization, etc.).

* ``PuppeteerExtra`` - The constructor, exposed for advanced
  usage.

Loading puppeteer-extras
~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: javascript

    var pe = require("puppeteer-extras");

The bundle is self-contained — no ``node_modules/`` lookup
required at runtime.  License attribution for all 37 packaged
npm packages (each with its original LICENSE text, version,
author, and license type from ``package.json``) is included as a
comment block at the top of the file.

Use with rampart-chromeview
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The integration pattern matches the one used by npm scripts
built on ``puppeteer``: ``addExtra`` wraps a launcher, ``use``
registers a plugin, and ``launch`` then starts a browser.
``rampart-chromeview`` itself is the launcher passed to
``addExtra``:

.. code-block:: javascript

    "use transpiler"
    var rch       = require("rampart-chromeview");
    var pe        = require("puppeteer-extras");

    var puppeteer = pe.addExtra(rch);
    puppeteer.use(pe.StealthPlugin());

    async function main() {
        var browser = await puppeteer.launch({headless: true});
        var page    = await browser.newPage();
        await page.goto("about:blank");

        /* The stealth plugin's evasions have already been injected
           via evaluateOnNewDocument before any page script runs.   */
        var webdriver = await page.evaluate("navigator.webdriver");
        var ua        = await page.evaluate("navigator.userAgent");
        rampart.utils.printf("webdriver: %J\n",  webdriver);
        rampart.utils.printf("userAgent: %s\n",  ua);
        await browser.close();
    }
    main().catch(function(e) { rampart.utils.printf("FAIL: %s\n", e); });

A run with stealth active produces output of the form:

.. code-block:: text

    webdriver: false
    userAgent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36
                (KHTML, like Gecko) Chrome/147.0.0.0 Safari/537.36

``navigator.webdriver`` no longer returns ``true``, and the
``HeadlessChrome`` token is absent from the user-agent — the two
standard fingerprint markers vanilla-headless Chrome exposes are
both suppressed by the stealth plugin's evasions.

Why "use transpiler" is required
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The stealth evasions are JavaScript functions injected into the
running page via ``page.evaluateOnNewDocument(fn)``.  Chrome
executes them in its own JavaScript engine, not in Rampart's, so
the **original ES2015+ source text** of each evasion is required
(the bundled transpiled body contains ``_TrN_Sp.*`` helper
references that Chrome does not have).  The transpiler stamps
each function with its original source via the
``_TrN_Sp._fs(fn, "src")`` mechanism; rampart-chromeview's
internal ``_requireSource`` routine reads ``fn.__source__`` and
forwards that source string to Chrome.

Scripts that use ``puppeteer-extras`` must therefore be run with
the transpiler active, supplied either as ``"use transpiler"`` at
the top of the file or as the ``-t`` command-line flag.

Bundled packages
~~~~~~~~~~~~~~~~

The 37 npm packages baked into the bundle (each retains its
original license, all permissive — MIT or ISC):

``arr-union``, ``balanced-match``, ``brace-expansion``,
``clone-deep``, ``concat-map``, ``debug``, ``deepmerge``,
``for-in``, ``for-own``, ``fs-extra``, ``fs.realpath``, ``glob``,
``graceful-fs``, ``inflight``, ``inherits``, ``is-buffer``,
``is-extendable``, ``is-plain-object``, ``isobject``,
``jsonfile``, ``kind-of``, ``lazy-cache``, ``merge-deep``,
``minimatch``, ``mixin-object``, ``ms``, ``once``,
``path-is-absolute``, ``puppeteer-extra``,
``puppeteer-extra-plugin``, ``puppeteer-extra-plugin-stealth``,
``puppeteer-extra-plugin-user-data-dir``,
``puppeteer-extra-plugin-user-preferences``, ``rimraf``,
``shallow-clone``, ``universalify``, ``wrappy``.

License: each package retains its original copyright and
license.  The bundle's header inlines every package's ``LICENSE``
file verbatim.

Why a bundle
~~~~~~~~~~~~

The rationale for shipping a pre-built single-file bundle instead
of relying on a separate ``npm install puppeteer-extra`` step:

* No ``node_modules`` tree is required.  The single file is
  placed on the rampart module search path and resolved via the
  standard ``require()`` mechanism.

* The installed version is reproducible: every install resolves
  to the same vendored set of package versions, eliminating the
  risk of a transitive-dependency change silently altering
  stealth behaviour.

* License attribution is aggregated.  Every bundled package's
  ``LICENSE`` text is inlined in the bundle header, satisfying
  distribution requirements without a separate ``licenses/``
  directory.

* The transitive closure is contained in a single file, which
  simplifies source review and auditing.

Scripts that prefer the upstream npm packages directly are still
supported: a ``node_modules`` tree placed next to the script
allows ``require("puppeteer-extra")`` and
``require("puppeteer-extra-plugin-stealth")`` to resolve via the
standard module-lookup path.  The bundle is an alternative
deliverable, not a replacement.
