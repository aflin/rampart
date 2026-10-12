# Rampart Overview for LLM Coding Assistants

## What is Rampart?

Rampart is a server-side JavaScript runtime built on the Duktape engine. It is
**not** Node.js — different engine, module system, threading model and built-ins.
Do not assume Node.js APIs, patterns, or npm packages will work.

Its design: JavaScript orchestrates high-performance C. The JS interpreter is
slower than V8; the C-backed modules (SQL, HTTP server, crypto) carry the
performance. Prefer a C-backed API over a JS loop wherever both exist.

## Critical Differences from Node.js

### No npm. No node_modules.
There is no package manager. Functionality comes from built-in globals and
included C modules. Pure-JS libraries may work but must be tested; a partial
Node compatibility layer exists for libraries that expect one (see
*Lazy-loaded surfaces*).

### Synchronous by default
Most operations block. `readFile()` returns data, `curl.fetch()` returns a
response, `sql.exec()` returns rows. Async variants exist for curl and redis
but must be opted into.

### Module system
Uses CommonJS-style `require()` but with a different search path:
1. Absolute path (checked alone; never resolved from a bundle)
2. When running a single-file bundle, the bundle's appended zip
3. Calling module's directory
4. `process.scriptPath`
5. `~/.rampart/`
6. `$RAMPART_PATH`
7. `process.installPath` (its `modules/` is `process.modulesPath`)

Each directory entry above is checked directly, then in its `modules/` and
`lib/rampart_modules/` subdirectories.

No `node_modules` directory. No ES module `import/export` without a transpiler.

### Threading model
Rampart has real POSIX threads via `new rampart.thread()`. Each thread gets its
own JS interpreter. The HTTP server automatically dispatches requests across a
thread pool. Threads do not share JS state — use SQL, LMDB, Redis, or the
thread clipboard (`rampart.thread.put/get`) for shared data.

### ECMAScript support
Duktape supports partial ES2015/ES2016 natively. For async/await, arrow
functions, destructuring and classes, add `"use transpiler"` (fast, C-based) or
`"use babel"` (slower, more complete) at the top of the script; output is cached
to disk. `-t` on the command line does the same.

**Write ES5 by default.** No built-in module or standard pattern needs
post-ES5 syntax — do not add a transpiler pragma unless the user asks for
ES2015+ (or the code goes through `rampart-nodeshim`, which needs it).

Transpiler gotchas: `const` becomes `var` (not enforced at runtime); with
`"use transpiler"`, `await` inside a loop may not run per-iteration, and
destructuring combined with `await` may fail.

## Architecture: What's Built-in vs What Requires Loading

### Built into the executable (no require needed)
- `rampart.utils` — file I/O, printf/sprintf, exec/shell, fork/daemon, stat,
  sleep, hexify/dehexify, stringToBuffer/bufferToString, CSV import, date
  functions, and much more. **This is the workhorse module.**
- `rampart.thread` — POSIX threads, locks, thread clipboard
- `rampart.vector` — typed vectors, distance metrics for semantic search
- `rampart.event` — cross-thread event system
- `rampart.import` — CSV parsing

### C modules (require() to load)
- `rampart-server` — multi-threaded HTTP/HTTPS/WebSocket server
- `rampart-sql` — Texis SQL database with Metamorph full-text search
- `rampart-curl` — HTTP/FTP/SMTP client (libcurl)
- `rampart-crypto` — encryption, hashing, HMAC (OpenSSL)
- `rampart-html` — HTML parsing, cleanup, DOM manipulation (Tidy-HTML5)
- `rampart-lmdb` — fast key-value store (LMDB)
- `rampart-redis` — Redis client
- `rampart-net` — TCP sockets with SSL/TLS
- `rampart-python` — embedded Python interpreter with type conversion
- `rampart-totext` — text extraction from DOCX, PDF, HTML, RTF, EPUB, etc.
- `rampart-cmark` — CommonMark Markdown to HTML
- `rampart-url` — URL parsing and resolution
- `rampart-gm` — image processing (GraphicsMagick)
- `rampart-robots` — robots.txt compliance checking
- `rampart-almanac` — celestial calculations, holidays, weather
- `rampart-auth` — session-based authentication for the HTTP server
- `rampart-treesitter` — source code parsing / symbol extraction
- `rampart-webserver` — the `--server` machinery as a module (`cmdLine()`)
- `rampart-cmodule` — compile and load a C module at runtime

### Lazy-loaded surfaces (no require needed)
These load automatically the first time a name is referenced, so scripts that
never touch them pay no startup cost:

- **Web Platform globals** (`rampart-whatwg`) — `fetch`, `URL`, `Headers`/
  `Request`/`Response`/`FormData`, `Blob`/`File`, the stream family,
  `WebSocket`, `XMLHttpRequest`, `crypto` (Web Crypto), `structuredClone`,
  `queueMicrotask`, `localStorage`. Conformance is **partial and
  experimental** — strongest for the non-DOM APIs. See
  *WHATWG / W3C Web Platform APIs* in `rampart-main.rst`.
- **`Intl`** (`rampart-intl`) — vendored ICU4C. See `rampart-main.rst`.
- **Node compatibility** (`rampart-nodeshim`) — backs `require('fs')`,
  `require('path')`, `require('http')`, `require('stream')`,
  `require('child_process')`, `require('worker_threads')` and friends.
  **Code running through nodeshim needs the transpiler (`-t`) in nearly all
  cases.** Coverage is partial and it is slower than the native APIs; see
  *rampart-nodeshim Module* in `rampart-extras.rst` for the per-submodule gaps.

## Common Patterns (from real-world code)

### The globalize pattern
Most scripts start with `rampart.globalize(rampart.utils);`, which makes
`printf`, `fprintf`, `sprintf`, `readFile`, `stat`, `exec`, `shell`, `fork`,
`sleep`, `fopen`, `fgets` etc. global. Code then reads like C:
`fprintf(stderr, "Error: %s\n", msg)`.

### Dual-mode scripts
One file can be both a web handler and a CLI setup tool — `module.exports` is
only set when loaded by the server:
```javascript
if(module && module.exports)
    module.exports = { "/": index_page, "/search.json": search_handler };
else
    build_the_database();      // rampart myscript.js
```

### Multi-path module exports
Export an Object to map several URLs from one script, or a single Function to
handle every request to it:
```javascript
module.exports = { "/": index_html, "/search.json": ajax_search };
module.exports = function(req) { return {json: {results: []}}; };
```

### printf format extensions
Beyond standard C printf codes, Rampart adds:
- `%J` / `%!J` — JSON (with optional indent width). `!` handles cyclic refs.
- `%B` / `%!B` — base64 encode/decode
- `%U` / `%!U` — URL encode/decode
- `%H` / `%!H` — HTML entity encode/decode
- `%P` — pretty-print text with wrapping

The `!` flag inverts the operation (encode vs decode) for `%B`, `%U`, `%H`.

All of `%s`, `%B`, `%U`, `%H` accept strings and any buffer type (plain buffer,
`Uint8Array`, `ArrayBuffer` or node `Buffer`). Without `!`, `%U` and `%B` also
accept Objects (converted to JSON first); `%H` does **not** — pass it a String
or Buffer, or it will throw.

`bprintf()` returns a Buffer. Use `bprintf('%s%s', buf1, buf2)` to concatenate
buffers of any type.

### HTML generation
The most common pattern uses template literals with sprintf format codes:
```javascript
return {html: `
<!DOCTYPE html>
<html><body>
  <h3>${%H:query}</h3>
  <pre>${%3J:results}</pre>
</body></html>
`};
```
`%H` HTML-escapes the variable, `%3J` pretty-prints JSON with 3-space indent.
This only works with `"use transpiler"` or without any transpiler (not babel).

For large responses, use the server buffer instead of string concatenation:
```javascript
function handler(req) {
    req.put('<!DOCTYPE HTML><html><body>');
    for(var i = 0; i < rows.length; i++) {
        req.printf('<div>%H</div>', rows[i].title);
    }
    return {html: '</body></html>'};
}
```

### HTTP server and the standard layout
The standard deployment uses `web_server_conf.js` or `rampart --server`:
```
web_server/
    web_server_conf.js   — edit this to configure
    html/                — static files (document root)
    apps/                — server-side JS modules (auto-mapped to /apps/)
    wsapps/              — WebSocket modules (auto-mapped to ws://wsapps/)
    data/                — application data (databases, etc.)
    logs/                — access and error logs
```
A module at `apps/search.js` is automatically served at `/apps/search.html`
or `/apps/search/`. No explicit route registration needed.

For custom routes, use `rampart-server` directly:
```javascript
var server = require("rampart-server");
server.start({
    bind: "0.0.0.0:8080",
    map: {
        "/":           "/path/to/html",
        "/api/data":   function(req) { return {json: {ok: true}}; },
        "ws:/chat":    function(req) { ... }
    }
});
```

### Request object
```javascript
function handler(req) {
    req.query.q          // URL query parameter ?q=...
    req.params.q         // merged: query + POST + cookies (use for flexibility)
    req.postData.content // parsed POST body (form data or JSON)
    req.formData.content // multipart file uploads (array)
    req.body             // raw body (Buffer)
    req.method           // "GET", "POST", etc.
    req.path.file        // requested filename
    req.path.path        // requested path
    req.ip               // client IP
    req.cookies          // parsed cookies
    req.headers          // request headers
}
```

### Response object
Return an object with a key matching the content type:
```javascript
return {html: "<h1>Hello</h1>"};
return {json: {status: "ok"}};
return {txt: "plain text"};
return {jpg: "@/path/to/image.jpg"};  // serve file with @ prefix
return {status: 302, headers: {"Location": "/newurl"}};  // redirect
return {status: 404, html: "Not found"};  // error
```

### SQL patterns
```javascript
var Sql = require("rampart-sql");
var sql = new Sql.connection("/path/to/db", true);  // true = create

// CREATE ... IF NOT EXISTS is supported for tables and indexes
sql.exec("CREATE TABLE IF NOT EXISTS docs (title VARCHAR(128), body VARCHAR(8000))");

// Insert with parameterized query
sql.exec("INSERT INTO docs VALUES(?, ?)", [title, body]);

// Single row lookup (returns object or undefined)
var row = sql.one("SELECT * FROM docs WHERE id = ?", [id]);

// Query with maxRows (default is 10!)
var results = sql.exec("SELECT * FROM docs", {maxRows: -1});  // -1 = all

// Row-by-row callback (return false to stop early)
sql.exec("SELECT * FROM docs WHERE body LIKEP ?", {maxRows: 100},
    [searchTerms],
    function(row, i, cols, info) {
        // process each row; i is 0-based index
        if(done) return false;
    }
);

// Full-text search — \uword indexes whole UTF-8 characters (any script)
sql.exec("CREATE FULLTEXT INDEX docs_ftx ON docs(body) " +
    "WITH WORDEXPRESSIONS ('[\\uword]{1,99}')");
// or to also match email addresses, URLs, etc.:
// "WITH WORDEXPRESSIONS ('[\\uword]{1,99}', '[\\alnum\\$%@\\-_\\+]{2,99}')"
var results = sql.exec("SELECT * FROM docs WHERE body LIKEP ?",
    ["search terms"], {maxRows: 50});

// Tune full-text search
sql.set({
    likeprows: 100,      // candidates for full-text ranking
    minwordlen: 5,       // minimum word length for suffix processing
    useequivs: true      // enable synonym expansion
});
```
- `sql.exec()` throws on hard errors (bad syntax) but NOT on soft errors
  (duplicate on unique index). Check `sql.errMsg` after calls.
- `sql.query()` never throws — always check `sql.errMsg`.
- Always use `?` parameterized queries for user input.

### Data directory resolution
Apps locate their data dir from `serverConf` when served, falling back to a
path relative to the script when run from the CLI:
```javascript
var db_location = (global.serverConf && serverConf.dataRoot)
    ? serverConf.dataRoot + "/mydb"
    : process.scriptPath + "/../data/mydb";
```

### Embeddings and semantic search (inside SQL)
Embedding runs **inside the SQL engine**. Load a model once on the connection,
then `embed()` and `LIKEV` use it automatically.

```javascript
// pick ONE embedding engine per connection
sql.set({llamaEmbed: "/path/model.gguf"});   // preferred — see note below

sql.exec("CREATE TABLE docs (title varchar(128), doc varchar(8000), v varvecF32(384))");

// embed() turns text into a vector; ? params work as usual
sql.exec("INSERT INTO docs VALUES(?, ?, embed(?))", [title, body, body]);

// LIKEV takes a vector, or a STRING which is auto-embedded with the same model
var hits = sql.exec("SELECT title, $rank FROM docs WHERE v LIKEV ?", ["search text"]);
```

**Which engine:** `llamaEmbed` (llama.cpp GGUF) is the mature default and the
one to use unless told otherwise — it is GPU-accelerated by Metal on Apple
Silicon and CUDA on Linux. `onnxEmbed` (ONNX model directory) is CUDA-only, so
on macOS it always runs on the CPU. `clipEmbed` is for image/text CLIP models
(`embed(?, 'image')` takes a file path). All three are set the same way and are
mutually exclusive per connection.

Rows are returned **already ordered by rank** — no `ORDER BY` needed.

### Hybrid search: LIKEP + LIKEV (rank fusion)
A single `OR` fuses full-text and vector search using Reciprocal Rank Fusion,
which is usually better than either alone:

```javascript
var res = sql.exec(
    "SELECT id, title, $rank, $krank, $vrank FROM docs " +
    "WHERE doc LIKEP ? OR v LIKEV ?",
    [terms, terms], {maxRows: 20});
```
- `$rank` — fused positional score (not a calibrated relevance value)
- `$krank` — the keyword side's own score; `0` if that side did not match
- `$vrank` — the vector side's own score; `0` if that side did not match
- `$krank`/`$vrank` are meaningful **in the SELECT list only**
- candidate pool sizes: `likepRows` (keyword) and `likevRows` (vector)

For long documents, `chunkembed()` stores **all** of a document's chunk
vectors in one column so a match on any chunk finds the row.

**Gotcha:** without a vector index, a `LIKEV` side is *refused* rather than
scanned — the query still succeeds, but that half contributes nothing and the
only signal is a soft error in `sql.errMsg` ("Query on `v' would require
linear search"). A hybrid query then quietly returns keyword-only results.
Either build the index (`CREATE VECTOR INDEX`) or opt into the scan with
`sql.set({alLinear: true})`. Check `sql.errMsg` after vector queries.

Full detail: `rampart-sql.rst` — *Vector Indexes*, *Querying with LIKEV*,
*Hybrid keyword + vector queries (rank fusion)*. Knob reference (`llamaEmbed`,
`onnxEmbed`, `clipEmbed`, `likevRows`, `likepRows`, `likevCache`):
`sql-set.rst`. Function reference (`embed`, `chunkembed`, `vecdist`):
`sql-server-funcs.rst`.

### AI modules used directly (without SQL)
When you need embeddings, reranking or generation outside the database, the
langtools modules are separate `require()`s — all documented in
`rampart-langtools.rst`:

- `rampart-llamacpp` — `initEmbed()`, `initRerank()`, `initGen()` (text
  generation, chat, tool calling). GPU via Metal / CUDA.
- `rampart-onnx` — `initEmbed()`, `initRerank()`, plus general
  `initSession()` for arbitrary ONNX models and tokenizers. GPU is CUDA-only.
- `rampart-clip` — CLIP image and text embeddings in one shared space.
- `rampart-faiss` — standalone vector index (`openFactory()`, `addFp32()`,
  `searchFp32()`), independent of SQL vector indexes.
- `rampart-models` — resolves a short model name to a file, downloading from
  HuggingFace on first use: `models.get("bge-m3:q8_0")`. Use this rather than
  hard-coding paths.

### curl for HTTP requests
```javascript
var curl = require("rampart-curl");
var res = curl.fetch("https://api.example.com/data", {maxTime: 10});
if(res.status === 200) {
    var data = JSON.parse(res.text);
}
```

### WebSocket patterns
One handler serves the whole connection; `req.count == 0` is the connect call,
later calls carry client data. Per-connection state lives on `req` and persists
across messages.
```javascript
function chat(req) {
    if(req.count == 0) {                       // connect
        req.username = req.query.user || "anonymous";
        req.wsOnDisconnect(function() { /* cleanup */ });
        return;
    }
    req.wsSend("Echo: " + sprintf("%s", req.body));   // req.body is a Buffer
}                                                     // wsSend also takes an Object -> JSON
module.exports = chat;
```

### Misc
- **Password hashing:** `crypto.passToKeyIv({password, salt: crypto.sha1(s),
  iter: 10000}).key`. Session auth is in `rampart-auth.rst`.
- **Directory traversal:** validate with
  `realPath(f).startsWith(allowed_root)` inside a `try` (`realPath` throws on
  a missing file).

## Pitfalls to Watch For

1. **Don't write Node code.** `require('fs')` and friends *do* resolve via
   `rampart-nodeshim` (see *Lazy-loaded surfaces* above), but that layer is for
   running libraries that expect Node. Write new code against `rampart.utils`
   and the `rampart-*` modules.

2. **SQL 10-row default.** SELECT returns max 10 rows unless you set `maxRows`.

3. **`IF NOT EXISTS` works on CREATE, but there is no `DROP ... IF EXISTS`.**
   `CREATE TABLE IF NOT EXISTS` and `CREATE INDEX IF NOT EXISTS` are both
   supported and idempotent. `DROP TABLE IF EXISTS` is a syntax error — but it
   is not needed, because a plain `DROP TABLE` of a table that does not exist
   already succeeds silently.

4. **Server threads don't share state.** Module variables are per-thread.
   Use SQL, LMDB, Redis, or thread clipboard for shared data.

5. **Thread variable scoping.** Only globals that exist BEFORE
   `new rampart.thread()` are copied. Variables defined after are invisible.

6. **fgets default is 1 byte.** Use `fgets(handle, 4096)` to read a line.

7. **Event callback signature.** `rampart.event.on()` callbacks receive
   `(uservar, triggerval)` — the trigger value is the SECOND argument.

8. **TypedArrays do have the array methods.** `forEach`, `map`, `filter`,
   `sort`, `reduce`, `slice`, `fill`, `indexOf`, `join` and the rest are all
   present and work. (`map`/`filter`/`slice` return a new TypedArray, not an
   Array.) What is missing is elsewhere: `Buffer.readBigInt64BE` and the other
   `Big*` accessors — see pitfall 9.

9. **Buffers are not Node.js Buffers.** See the buffer table in `faq.rst`
   for what works on which type. `Buffer.from()` accepts String, Buffer,
   ArrayBuffer, TypedArray, or Array. `Buffer.alloc(n, fill)` accepts
   Number, String, or Buffer fill.

## External Projects

Shipped with the binary and usable via `require()`: **langtools** (AI —
`rampart-langtools.rst`), **webview** (desktop apps — `rampart-webview.rst`),
**iroh** and **iroh-webproxy** (P2P networking), **lang derivs** (multilingual
suffix rules, English included).

Separate repos under `github.com/aflin`, not installed: `rampart_webdav`,
`rampart_webshield`, `Self_Hosted_Search_Engine`, `rampart_wikipedia_search`,
`rampart_docs`.

## Documentation Map

Detailed docs are in reStructuredText files in the `source/` directory:

| Topic | File |
|-------|------|
| Core runtime, globals, require, events, transpiler | `rampart-main.rst` |
| Utility functions (printf, file I/O, exec, fork, dates, HLL) | `rampart-utils.rst` |
| Threads, locks, thread clipboard | `rampart-thread.rst` |
| HTTP server (routes, request/response, WebSocket) | `rampart-server.rst` |
| SQL database and full-text search | `rampart-sql.rst` (via `sqltoc.rst`) |
| SQL string functions (rex, sandr, stringFormat, abstract) | `rampart-sql.rst` |
| SQL command line utilities (tsql, kdbfchk, addtable) | `sql-utils.rst` |
| Session authentication | `rampart-auth.rst` |
| HTTP client (curl) | `rampart-curl.rst` |
| Crypto (OpenSSL) | `rampart-crypto.rst` |
| HTML parsing and DOM manipulation | `rampart-html.rst` |
| Markdown to HTML | `rampart-cmark.rst` |
| URL parsing and resolution | `rampart-url.rst` |
| robots.txt compliance | `rampart-robots.rst` |
| LMDB key-value store | `rampart-lmdb.rst` |
| Redis client | `rampart-redis.rst` |
| TCP/SSL sockets | `rampart-net.rst` |
| Python interop | `rampart-python.rst` |
| Text extraction (DOCX, PDF, etc.) | `rampart-totext.rst` |
| Image processing (GraphicsMagick) | `rampart-gm.rst` |
| Vector operations and semantic search | `rampart-vector.rst` |
| Celestial calculations, holidays, weather | `rampart-almanac.rst` |
| Source code parsing (tree-sitter) | `rampart-treesitter.rst` |
| AI modules: llamacpp (embed/rerank/gen), onnx, clip, faiss, models | `rampart-langtools.rst` |
| SQL embedding knobs (llamaEmbed, onnxEmbed, clipEmbed, likev*) | `sql-set.rst` |
| SQL functions (embed, chunkembed, vecdist, abstract, rex) | `sql-server-funcs.rst` |
| Web Platform globals (fetch, URL, Blob, streams) and `Intl` | `rampart-main.rst` |
| Node compatibility layer (`require('fs')` etc., needs `-t`) | `rampart-extras.rst` |
| Desktop apps (webview) | `rampart-webview.rst` |
| Headless Chrome automation | `rampart-chromeview.rst` |
| Rex pattern matching (**not** Perl regex) | `rex-sandr.md` |
| FAQ, buffer table, deployment, gotchas | `faq.rst` |
| Extras (webserver module, LLM module) | `rampart-extras.rst` |
| Tutorials (citysearch, wschat, pi_news) | `tutorial-*.rst` |
