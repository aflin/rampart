# Rampart Overview for LLM Coding Assistants

Covers Rampart **v0.7.6**.  Read this file first; it tells you what Rampart
is, what not to assume, and **where in the documentation to look** for
everything else.

## What is Rampart?

Rampart is a server-side JavaScript runtime built on the Duktape engine.  It
is **not** Node.js: different engine, module system, threading model and
built-ins.  Do not assume Node.js APIs, patterns, or npm packages.

Its design: JavaScript orchestrates high-performance C.  The interpreter is
slower than V8; the C-backed modules (SQL and search, HTTP server, crypto)
carry the performance.  Prefer a C-backed API over a JS loop wherever both
exist.

## How to Use This Documentation

### Where the files are

| Location | Path |
|----------|------|
| Any Rampart install | `<installPath>/web_server/ide/docs/source/` (`rampart -c "console.log(process.installPath)"`) |
| The `rampart_docs` repository | `source/` |
| Online | `https://rampart.dev/docs/<file>.html` (e.g. `rampart-sql.html`) |

Each module has one reStructuredText file.  This file and `rex-sandr.md` are
the only Markdown files.

### How to find something

**Do not read the large files whole.**  `rampart-utils.rst`, `rampart-sql.rst`,
`rampart-langtools.rst`, `rampart-server.rst` and `sql-set.rst` are 3,500 to
5,500 lines each.  Find the section, then read that range.

- Every function, option group and SQL setting is its own heading, and the
  heading is the name.  List a file's headings with line numbers:

      grep -n -B1 '^[=~"-]\{4,\}$' rampart-server.rst

- Jump to one function or setting (headings start at column 0):

      grep -n '^fgets' rampart-utils.rst        # a rampart.utils function
      grep -n '^exec()' rampart-sql.rst         # sql methods end in ()
      grep -n '^likepRows' sql-set.rst          # a sql.set() setting

- Search every file for a name: `grep -n 'threadLimit' *.rst`
- `faq.rst` is organized as questions.  If the task matches one ("How do I
  handle POST data and file uploads?"), start there; it links onward.
- Working code: `tutorial-citysearch.rst` (SQL, CSV import, geographic
  search), `tutorial-wschat.rst` (WebSockets, Redis) and
  `tutorial-pi_news.rst` (crawling, HTML parsing); the sample site in
  `<installPath>/web_server/`, and `<installPath>/test/*-test.js` (the test
  suite uses nearly every API).

### Documentation map

Section names are quoted exactly as they appear, so they can be grepped.

**Language and runtime**

| Need | File | Sections |
|------|------|----------|
| Globals, `process.*`, `require()`, search path | `rampart-main.rst` | *Rampart Global Variable and Functions*, *Process Global Variable and Functions*, *Module Search Path* |
| What JavaScript is supported without a transpiler | `rampart-main.rst` | *Extra JavaScript Functionality* (*Template Literals*, *Rest Parameters*, *Map and Set*, *BigInt*, *TypedArray Methods* ...) |
| ES2015+ syntax | `rampart-main.rst` | *ECMAScript 2015+ with transpiler*, *ECMAScript 2015+ and Babel.js* |
| Timers and the event loop | `rampart-main.rst` | *setTimeout()*, *setInterval()*, *setMetronome()* |
| Cross-thread events | `rampart-main.rst` | *rampart.event* |
| CSV parsing | `rampart-main.rst` | *rampart.import* |
| `fetch`, `URL`, `Blob`, streams, Web Crypto, `Intl` | `rampart-main.rst` | *WHATWG / W3C Web Platform APIs (experimental)*, *Intl* |
| Threads, locks, thread clipboard | `rampart-thread.rst` | *Thread Functions*, *Lock Functions* |
| Typed vectors and distances in JavaScript (`rampart.vector`) | `rampart-vector.rst` | *Typed Vectors*, *Distance Function* |
| Running Node-style libraries | `rampart-extras.rst` | *rampart-nodeshim Module* |
| Writing a module in C | `rampart-extras.rst`, `rampart-server.rst` | *rampart-cmodule*, *Rampart Macros for C Modules*, *C-API* |

**`rampart.utils`** (`rampart-utils.rst`, one heading per function)

| Need | Sections |
|------|----------|
| Formatted output | *printf*, *sprintf*, *bprintf*, *abprintf* |
| Files and directories | *readFile*, *writeFile*, *stat*, *readDir*, *walkDir*, *glob*, *copyFile*, *rmFile*, *mkdir*, *realPath* |
| File handles and line reading | *File Handle Utilities* (*fopen*, *fgets*, *readLine*, *fwrite* ...) |
| Raw descriptors, atomic writes | *POSIX File Descriptor I/O* |
| Processes | *exec*, *shell*, *fork*, *daemon*, *newPipe*, *kill* |
| Strings, buffers, encodings | *stringToBuffer*, *bufferToString*, *hexify*, *toUtf8*, *detectCharset*, *trim* |
| URLs and query strings | *urlComponents*, *absUrl*, *objectToQuery*, *queryToObject* |
| Dates | *dateFmt*, *scanDate*, *autoScanDate* |
| Zip, gzip, checksums | *zipList*, *zipGet*, *zipExtract*, *Compression and Checksums* |
| Watching files for changes | *File Watching* |
| Random numbers, hashing, HyperLogLog | *Rand, Hash and HyperLogLog* |

**Web server**

| Need | File | Sections |
|------|------|----------|
| Start a server, all options | `rampart-server.rst` | *Configuring and Starting the Server* |
| URL routing | `rampart-server.rst` | *Path Mapping* |
| `req` and the returned object | `rampart-server.rst` | *The Request Object*, *The Return Object*, *Key to Mime Mappings* |
| Large or streamed replies | `rampart-server.rst` | *Chunking Replies* |
| WebSockets | `rampart-server.rst`, `tutorial-wschat.rst` | *Websockets* |
| Reverse proxy | `rampart-server.rst` | *Reverse Proxy (Experimental)* |
| Standard directory layout, `web_server_conf.js`, several ports, HTTPS | `rampart-extras.rst` | *rampart-webserver.js Module* |
| Logins and sessions | `rampart-auth.rst` | *Quick Start*, *The req.userAuth Object* |
| One-file deployment | `rampart-extras.rst` | *Single-File Bundles* |
| Browser-based editor and terminal for a site | `rampart-extras.rst` | *The Mini IDE* |

**Database and search** (`rampart-sql.rst` unless noted)

| Need | Sections |
|------|----------|
| Connect, query, insert | *connection() constructor*, *exec()*, *one()*, *query()* |
| Read and change settings | *set()*, *get()*, *reset()*; every setting: `sql-set.rst` |
| Bulk import | *importCsvFile()*, *importJsonlFile()* |
| Column types, `$recid` | *Column Data Types* |
| Ordinary indexes | *Regular Indexes*, *Compound Indexes* |
| Full-text search | *Fulltext Indexes*, *Word Expressions*, *Text Index Maintenance*, *scheduleUpdate()* |
| Vector (semantic) search | *Vector Search*, *Vector Indexes*, *Querying with LIKEV* |
| Keyword + vector together | *Hybrid keyword + vector queries (rank fusion)* |
| SQL functions (`embed`, `chunkembed`, `abstract`, `excerpt`, JSON, geo) | `sql-server-funcs.rst` |
| Ranking and query limits | `sql-set.rst`: *Rank knobs*, *Other Ranking Properties*, *Query Protection* |
| Text helpers outside SQL (`rex`, `sandr`, `stringFormat`, `abstract`) | *String Functions*; pattern syntax: `rex-sandr.md` |
| Command line tools (`tsql`, `kdbfchk`, `addtable`) | `sql-utils.rst` |
| Key-value store | `rampart-lmdb.rst` |
| Redis client | `rampart-redis.rst` |

**Other modules** (each is a `require()`)

| Module | Purpose | File |
|--------|---------|------|
| `rampart-curl` | HTTP(S)/FTP/SMTP client; HTTP/2 | `rampart-curl.rst` |
| `rampart-net` | TCP/TLS sockets, servers, DNS, WebSocket client | `rampart-net.rst` |
| `rampart-crypto` | Ciphers, hashes, HMAC, RSA/EC/Ed25519, passwords, certificates | `rampart-crypto.rst` |
| `rampart-html` | Parse, repair, query and edit HTML; HTML to Markdown | `rampart-html.rst` |
| `rampart-cmark` | Markdown to HTML | `rampart-cmark.rst` |
| `rampart-totext` | Text from PDF, DOCX, XLSX, EPUB, email; OCR | `rampart-totext.rst` |
| `rampart-gm` | Image processing (GraphicsMagick) | `rampart-gm.rst` |
| `rampart-python` | Embedded Python | `rampart-python.rst` |
| `rampart-treesitter` | Parse source code, extract symbols | `rampart-treesitter.rst` |
| `rampart-robots` | robots.txt checks | `rampart-robots.rst` |
| `rampart-almanac` | Sun and moon, holidays, weather | `rampart-almanac.rst` |
| `rampart-email` | Send mail | `rampart-extras.rst` *rampart-email* |
| `rampart-llm` | Client for hosted or remote LLMs, with tool use | `rampart-extras.rst` *rampart-llm* |
| `rampart-llamacpp`, `rampart-onnx`, `rampart-clip`, `rampart-faiss`, `rampart-ocr`, `rampart-sentencepiece`, `rampart-models` | Local AI: embedding, reranking, generation, OCR, model download | `rampart-langtools.rst` |
| `rampart-chromeview` | Drive headless Chrome (Puppeteer-style) | `rampart-chromeview.rst` |
| `rampart-webview` | Desktop GUI apps | `rampart-webview.rst` |
| `rampart-url` | Superseded: use `rampart.utils.urlComponents` and `absUrl` | `rampart-utils.rst` |

Three files in `modules/` are internal parts of other modules; use them
through the module, not with their own `require()`:
`rampart-sqlUpdate.js` (scheduled index updates: `rampart-sql.rst`
*scheduleUpdate()*, *scheduleRebuild()*), and `rampart-open-meteo.js` and
`rampart-date-holidays.js` (`rampart-almanac.rst` *Weather*, *Holidays*).

Not every module is in every install.  `rampart --install --list` shows what
can be added (`rampart-langtools`, `rampart-iroh`, `rampart-webview` and
others); `rampart --install <name>` adds one.

## Critical Differences from Node.js

### No npm, no node_modules
Functionality comes from built-in globals and the modules above.  Pure-JS
libraries may work but must be tested.  `require('fs')`, `require('path')`
and friends resolve through `rampart-nodeshim`, which exists to run libraries
written for Node; it is partial, slower than the native APIs, and nearly
always needs the transpiler (`-t`).  Write new code against `rampart.utils`
and the `rampart-*` modules.

### Synchronous by default
Most calls block and return their result: `readFile()` returns the data,
`curl.fetch()` the response, `sql.exec()` the rows.  Async variants exist
(curl, redis, net, chromeview) but are opt-in.

### `require()` search order
1. An absolute path (checked alone)
2. In a single-file bundle, the bundle's zip
3. The calling module's directory
4. `process.scriptPath`
5. `~/.rampart/`
6. `$RAMPART_PATH`
7. `process.installPath`

Each directory is also checked in its `modules/` and `lib/rampart_modules/`
subdirectories.  **The current working directory is not searched.**  With no
extension, `.js`, `.json` and `.so` are tried in that order.

### Threads
`new rampart.thread()` creates a real POSIX thread with its own interpreter.
The HTTP server runs handlers across a thread pool.  **Threads do not share
JavaScript state.**  Share data through SQL, LMDB, Redis, or the thread
clipboard (`rampart.thread.put()` / `get()`).

### What JavaScript runs
**Write ES5 by default.**  No built-in module needs newer syntax.

| Works with no transpiler | Needs `"use transpiler"` or `-t` |
|--------------------------|----------------------------------|
| Template literals, tagged templates, `String.raw` | Arrow functions |
| Rest parameters (`function(a, ...rest)`) | `let`, `const` |
| `Promise`, `Map`, `Set`, `Symbol`, `Proxy`, `WeakMap` | Classes |
| `BigInt` (`123n`, operators) | Destructuring, spread, default parameters |
| TypedArray and modern Array/String/Object methods | `for...of`, `async`/`await`, generators |

Put `"use transpiler"` (fast, C-based) or `"use babel"` (slower, more
complete) as the first line of a script; the result is cached beside the
source as `name.transpiled.js`.

Transpiler caveat: `const` becomes `var`, so reassigning it does not throw.

## Common Patterns

### The globalize pattern
Most scripts start with `rampart.globalize(rampart.utils);`, which makes
`printf`, `sprintf`, `fprintf`, `readFile`, `stat`, `exec`, `shell`, `fopen`,
`sleep` and the rest global.  `rampart -g script.js` does the same.

### printf format extensions
Beyond the C codes:
- `%J` — JSON; a width indents (`%3J`).  Cyclic references are handled.
- `%B` / `%!B` — base64 encode / decode
- `%U` / `%!U` — URL encode / decode
- `%H` / `%!H` — HTML entity encode / decode
- `%P` — wrap and indent text
- `%s` — also accepts Buffers, and Objects (printed as JSON)

`%d` and the other numeric codes require a Number; a String throws.  `%H`
requires a String or Buffer.  `bprintf()` returns a Buffer; use
`bprintf('%s%s', buf1, buf2)` to join buffers.

### HTML generation
Template literals accept a printf code before a colon:
```javascript
return {html: `
<!DOCTYPE html>
<html><body>
  <h3>${%H:query}</h3>
  <pre>${%3J:results}</pre>
</body></html>`};
```
`%H` HTML-escapes the value — use it for anything that came from a user.
This works with no transpiler or with `"use transpiler"`, not with babel.

For large pages, write to the reply buffer instead of building a string:
```javascript
function handler(req) {
    req.put('<!DOCTYPE HTML><html><body>');
    for(var i = 0; i < rows.length; i++)
        req.printf('<div>%H</div>', rows[i].title);
    return {html: '</body></html>'};
}
```

### The standard web server layout
Every install has a working site at `<installPath>/web_server/`; copy it to
start a project.
```
web_server/
    web_server_conf.js   — configuration; `rampart web_server_conf.js start`
    html/                — static files (document root)
    apps/                — server-side scripts, mapped to /apps/
    wsapps/              — WebSocket scripts, mapped to ws://host/wsapps/
    data/                — databases and other application data
    logs/                — access and error logs
```
`apps/search.js` is served at `/apps/search.html` (or any extension) and
`/apps/search/`; no route registration is needed.  Edited scripts are
reloaded automatically.

`start-mini-ide.sh` in the same directory starts a browser-based editor,
terminal and searchable copy of this documentation.

### Scripts in `apps/`
Export one function, or an object that maps sub-paths:
```javascript
module.exports = function(req) { return {json: {ok: true}}; };
module.exports = { "/": index_page, "/search.json": search_handler };
```
The same file can serve requests and act as a command line tool:
```javascript
if(module && module.exports)
    module.exports = { "/": index_page };
else
    build_the_database();      // rampart apps/myscript.js
```
Find the data directory either way:
```javascript
var db_location = (global.serverConf && serverConf.dataRoot)
    ? serverConf.dataRoot + "/mydb"
    : process.scriptPath + "/../data/mydb";
```

### A server without the standard layout
```javascript
var server = require("rampart-server");
server.start({
    bind: "0.0.0.0:8080",
    map: {
        "/":           "/path/to/html",                // a directory of files
        "/api/data":   function(req) { return {json: {ok: true}}; },
        "/api/":       function(req) { ... },          // trailing / matches below it
        "ws:/chat":    function(req) { ... }
    }
});
```
A key without a trailing `/` matches that path exactly.

### The request object
```javascript
function handler(req) {
    req.query.q          // ?q=...   (repeated keys become an Array)
    req.postData.content // parsed POST body: form fields or JSON (Object)
    req.body             // raw body (Buffer)
    req.cookies.name     // parsed cookies
    req.headers          // request headers
    req.params.q         // query + POST + cookies + headers merged
    req.method           // "GET", "POST", ...
    req.path.path        // "/apps/search/sub/a.html"
    req.path.file        // "a.html"
    req.path.base        // "/apps/search/sub/"
    req.path.url         // full URL with query string
    req.ip               // client address
}
```
`req.params` is convenient but includes headers and cookies; read
`req.query` or `req.postData.content` when the source matters.

A multipart upload (`multipart/form-data`) also arrives in
`req.postData.content`, as an Array with one entry per part:
`{name, filename, "Content-Type", content}`, where `content` is a Buffer.
There is no `req.formData`.

### The return object
The key names the content type:
```javascript
return {html: "<h1>Hello</h1>"};
return {json: {status: "ok"}};
return {txt: "plain text"};
return {jpg: "@/path/to/image.jpg"};                 // @ serves a file
return {status: 302, headers: {location: "/new"}};   // redirect
return {status: 404, html: "Not found"};
```

### WebSockets
One function serves the whole connection.  `req.count == 0` is the connect
call; later calls carry a message in `req.body`.  Properties set on `req`
persist for the connection.
```javascript
module.exports = function(req) {
    if(req.count == 0) {
        req.username = req.query.user || "anonymous";
        req.wsOnDisconnect(function() { /* cleanup */ });
        return;
    }
    req.wsSend("Echo: " + sprintf("%s", req.body));   // an Object is sent as JSON
};
```

### SQL
```javascript
var Sql = require("rampart-sql");
var sql = new Sql.connection("/path/to/db", true);   // true = create if missing

sql.exec("CREATE TABLE IF NOT EXISTS docs (title VARCHAR(128), body VARCHAR(8000))");
sql.exec("INSERT INTO docs VALUES(?, ?)", [title, body]);

var res = sql.exec("SELECT * FROM docs", {maxRows: 50});
// res = {columns: [...], rows: [{title:..., body:...}, ...], rowCount: n}

var row = sql.one("SELECT * FROM docs WHERE title = ?", [t]);   // Object or undefined

// one row at a time; return false to stop
sql.exec("SELECT * FROM docs", {maxRows: -1}, function(row, i, cols, info) { });

// the id of a new row
var id = sql.exec("INSERT INTO docs VALUES(?, ?)", [t, b], {withId: true}).rows[0].$recid;
```
- **`maxRows` defaults to 10.**  Pass `{maxRows: -1}` for every row.
- `sql.exec()` throws on a hard error (bad syntax) but not on a soft one,
  such as a duplicate in a unique index.  Check `sql.errMsg` after writes.
- `sql.query()` never throws; always check `sql.errMsg`.
- Use `?` parameters for anything supplied by a user.
- `sql.set()` wants real types: `true`/`false` for booleans and whole
  numbers for integers.  An unknown setting name throws.

### Full-text search
```javascript
// \uword matches whole UTF-8 characters, so any language is indexed
sql.exec("CREATE FULLTEXT INDEX docs_ftx ON docs(body) " +
    "WITH WORDEXPRESSIONS ('[\\uword]{1,99}')");

var res = sql.exec("SELECT title FROM docs WHERE body LIKEP ?",
    ["search terms"], {maxRows: 20});
```
`LIKEP` returns rows best match first.  **Do not add `ORDER BY $rank`**; it
is slower and changes nothing.  `LIKEP` ranks the best `likepRows`
candidates (100 by default), so it is for "the best results", not for
counting every match.

### Semantic and hybrid search
Embedding runs inside the SQL engine.  Load a model on the connection, then
`embed()` and `LIKEV` use it.
```javascript
var models = require("rampart-models");
sql.set({llamaEmbed: models.get("bge-m3:q8_0")});    // downloads on first use

sql.exec("CREATE TABLE docs (title varchar(128), doc varchar(8000), v varvecF32(1024))");
sql.exec("INSERT INTO docs VALUES(?, ?, embed(?))", [title, body, body]);
// the default backend (IVFPQ) needs about 10,000 rows; HNSW suits small tables
sql.exec("CREATE VECTOR INDEX docs_v ON docs(v) WITH backend 'hnsw'");

// LIKEV takes a vector, or a String that is embedded with the same model
var hits = sql.exec("SELECT title, $rank FROM docs WHERE v LIKEV ?", ["search text"]);

// keyword and vector results fused (Reciprocal Rank Fusion)
var both = sql.exec("SELECT title, $rank, $krank, $vrank FROM docs " +
    "WHERE doc LIKEP ? OR v LIKEV ?", [terms, terms], {maxRows: 20});
```
- The vector column's size must match the model's dimension.
- Engines: `llamaEmbed` (GGUF; the default choice, GPU on Apple Silicon and
  CUDA), `onnxEmbed`, `clipEmbed` (images and text).  One per connection.
- For long documents, `chunkembed()` stores a vector per chunk in one
  column; `excerpt()` returns the matching passage.
- **Without a vector index, `LIKEV` is refused rather than scanned.**  The
  query still succeeds, a hybrid query quietly returns keyword-only rows,
  and the only sign is a message in `sql.errMsg`.  Build the index, or allow
  the scan with `sql.set({alLinear: true})`.

Details: `rampart-sql.rst` *Vector Search* and *Hybrid keyword + vector
queries (rank fusion)*; `faq.rst` *Which embedding engine should I use?*

### AI modules outside SQL
All in `rampart-langtools.rst`:
- `rampart-llamacpp` — `initEmbed()`, `initRerank()`, `initGen()` (generation,
  chat, tool calling)
- `rampart-onnx` — embedding, reranking, arbitrary ONNX sessions
- `rampart-clip` — image and text embeddings in one space
- `rampart-ocr` — text from images and scanned pages
- `rampart-faiss` — a standalone vector index
- `rampart-models` — `models.get("name")` resolves a model name to a local
  file, downloading it on first use.  Use it instead of hard-coded paths.

For a hosted model, or one on another machine, use `rampart-llm`
(`rampart-extras.rst`).

### HTTP requests
```javascript
var curl = require("rampart-curl");
var res = curl.fetch("https://api.example.com/data", {maxTime: 10});
if(res.status == 200)
    var data = JSON.parse(res.text);     // res.body is the Buffer
else
    printf("failed: %s\n", res.errMsg);  // status 0 = no response
```
POST with `{post: {a: 1}}` (form) or `{post: jsonString, headers:
["Content-Type: application/json"]}`.

### Passwords and paths
- Hash: `crypto.passwd(password).line`; verify:
  `crypto.passwdCheck(line, password)`.  For logins, use `rampart-auth`
  rather than writing your own.
- Before opening a user-supplied path, confirm
  `realPath(f).startsWith(allowed_root)` inside a `try` (`realPath` throws
  if the file is missing).

## Pitfalls

1. **`readFile()` returns a Buffer.**  Pass `{returnString: true}` for text,
   or convert with `bufferToString()`.
2. **`sql.exec()` returns at most 10 rows** unless `maxRows` is set.
3. **`stat()` returns `false` for a missing file**; `readFile()` and
   `realPath()` throw.
4. **`fgets(handle)` reads one byte.**  Use `fgets(handle, 4096)`, or
   `readLine(file).next()`.
5. **Threads see only globals that existed when the thread was created.**
   Define shared functions and values before `new rampart.thread()`.
6. **Server handlers run in many threads.**  A module-level variable is one
   copy per thread, so a counter or cache kept there is not shared.
7. **`eval()` runs in the global scope.**  It cannot read a function's local
   variables, and a `var` it declares becomes a global.  To pass a local
   value in, use `new Function("x", code)(value)`.
8. **`DROP TABLE IF EXISTS` is a syntax error.**  `CREATE ... IF NOT EXISTS`
   is supported.  A plain `DROP TABLE` of a missing table does not throw; it
   only sets `sql.errMsg`.
9. **`rampart.event.on()` callbacks receive `(uservar, triggerval)`.**  The
   value passed to `trigger()` is the second argument.
10. **Buffers are not Node Buffers.**  `Buffer.readBigInt64BE` and the other
    `Big*` accessors are missing.  See `faq.rst` *What buffer types are
    available and how do they differ?*
11. **Rex is not regular expressions.**  `Sql.rex()` and `sandr()` use their
    own syntax (`rex-sandr.md`).  JavaScript `RegExp` works as usual, and
    `Sql.re2()` takes RE2 syntax.
12. **The transpiler cache is keyed on the source file's time.**  After
    upgrading Rampart, delete stale `*.transpiled.js` files if transpiled
    code misbehaves.

## Changed Recently

If your knowledge of Rampart predates these releases:

- **0.7.6** — `sql.importJsonl()` / `importJsonlFile()`; `lockTable` option
  for imports; `UNION` works.
- **0.7.5** — `$recid` is a Number; `{withId: true}` returns the ids of rows
  written; an empty `varchar` or blob column reads back as `""`, not `null`;
  `html.toMarkdown()`; `fprintf()` writes nothing if formatting fails;
  rampart-sql is under the TSAL-1.0 license (`LICENSE-tsal.txt`).
- **0.7.4** — `curl` uses HTTP/2 when the server offers it;
  `toLocaleString()` and friends honor a locale through `Intl`; the server's
  reverse proxy streams replies.
- **0.7.3** — the sample web server, Mini IDE and documentation come with
  every install.
- **0.7.2** — `sql.get()` and `sql.reset()`; `sql.set()` type checking;
  server `threadLimit` and multi-listener `listen`;
  `browser.fetch()` in chromeview.
- `await` inside loops and destructuring an awaited value now work under
  `"use transpiler"`.
- `crypto.JSBI` and `rampart-url` are superseded by native `BigInt` and
  `rampart.utils.urlComponents`.
