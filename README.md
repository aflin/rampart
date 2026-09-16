# Rampart

**Squish the stack.**

Rampart is a complete JavaScript application platform in a single, tiny package: a
multi-threaded HTTP/HTTPS server, a SQL database with fully integrated full-text *and*
vector search, local AI inference, a key-value store, crypto, networking, document text
extraction, real OS threads, and a deep C-backed standard library.

No database server to run. No search cluster. No cache tier. No `node_modules`. One
process, tens of megabytes of RAM — [rampart.dev](https://rampart.dev/) itself is served
from a Raspberry Pi Zero.

- **Website:** <https://rampart.dev/>
- **Documentation:** <https://rampart.dev/docs/>
- **Downloads:** <https://rampart.dev/downloads/>
- **Demos:** <https://rampart.dev/demos.html>

---

## Install

```sh
curl -fsSL https://get.rampart.dev/ | sh
```

The installer detects your platform, downloads the matching build, verifies its checksum
and installs it. Without `sudo` it installs to `~/.rampart`; for a system-wide install to
`/usr/local/rampart`, pipe to `sudo sh` instead (files stay owned by the invoking user, so
later maintenance does not need root).

Pre-built binaries are available for:

- Linux x86_64 (glibc 2.17+)
- Linux ARM64 / aarch64 (glibc 2.17+), including 64-bit Raspberry Pi OS
- Raspberry Pi 32-bit, Raspberry Pi OS / Debian 10 (Buster) and up — one build for
  every 32-bit Pi, down to the Pi Zero
- macOS Apple Silicon and Intel, Big Sur and up
- FreeBSD 14 (experimental)

Prefer to place the files yourself? Grab a tarball from
[rampart.dev/downloads/](https://rampart.dev/downloads/) and either run the bundled
`./install.sh` or run it in place — Rampart needs no installation step as long as the
extracted directory structure is intact.

The base install carries the core modules. The rest — Python interop, langtools,
tree-sitter, chromeview and so on — are fetched on demand:

```sh
rampart --install --list                        # show what is available
rampart --install rampart-langtools             # install named modules
rampart --install all                           # install everything
```

Where a module has hardware-specific builds, each is its own name:
`rampart-langtools-cu11`, `-cu12` and `-cu13` for CUDA, and
`rampart-langtools-arm8a` for a faster ARMv8-A build on a Pi 3 or newer.
Installing one re-points `rampart-llamacpp.so`, `rampart-faiss.so` and
`rampart-clip.so` at its builds; switch back by installing a different variant.

## Hello, world

```sh
rampart myscript.js          # run a script
rampart                      # interactive REPL
rampart --quickserver        # serve the current directory on :8088
rampart --server ~/web_server/   # start the full, daemonized server
```

```javascript
var server = require("rampart-server");

server.start({
    bind: "127.0.0.1:8088",
    map: {
        "/": "/path/to/html",
        "/api/time": function (req)
        {
            return { json: { now: new Date() } };
        },
        "ws:/echo": function (req)
        {
            if (req.count)
                req.wsSend(req.body);
        }
    }
});
```

## Hybrid search in one SQL statement

Keyword search and semantic search live in the same engine, so a single `WHERE` clause
runs both retrievers and fuses their ranked lists with Reciprocal Rank Fusion. Embeddings
are generated inside the SQL engine from a local `.gguf` model — no external service, no
API key, no vector database.

**Build the index:**

```javascript
var Sql = require("rampart-sql");
var totext = require("rampart-totext");

var sql = Sql.connect("/path/to/data/docs", true);

sql.exec("create table docs (Url varchar(96), Title varchar(64), " +
         "Text varchar(16000), Vec varvecF16)");

sql.set({ llamaEmbed: "/path/to/models/all-minilm-l6-v2_f16.gguf" });

rampart.utils.walkDir("/path/to/html", function (path, type)
{
    if (type !== "file" || !/\.(html?|pdf|docx?|txt)$/i.test(path))
        return;

    var text = totext.convertFile(path);
    var title = path.replace(/.*\//, "");

    /* chunkembed() splits the document and embeds every chunk */
    sql.exec("insert into docs values (?, ?, ?, chunkembed(?, '', ?))",
             [ path, title, text, text, title ]);
});

sql.exec("create metamorph inverted index docs_Text_mmix on docs(Text)");
sql.exec("create vector index docs_Vec_vx on docs(Vec) with backend 'hnsw'");
```

**Query it:**

```javascript
var res = sql.query(
    "select $rank score, $krank krank, $vrank vrank, Url, Title, " +
    "abstract(Text, 400, 'querybest', ?, Vec) Snip " +
    "from docs where Text likep ? or Vec likev ?",
    [ q, q, q ], { maxRows: 20 });
```

Rows arrive deduplicated and already in fused order — no `ORDER BY`. `$krank` and
`$vrank` expose what each retriever scored on its own, and `abstract()` snips the chunk
that actually matched.

## Real threads, not workers

`rampart.thread` starts an actual POSIX thread with its own JavaScript interpreter
and its own event loop. There is no message-passing boundary to design around: you
hand `exec()` a function, it runs on another core, and its return value arrives in a
callback on the calling thread's event loop — as ordinary as `setTimeout()`, but
running in parallel.

```javascript
var thr = new rampart.thread();

thr.exec(
    function (dir) {
        /* a real interpreter: require() works here */
        var totext = require("rampart-totext");
        var out = [];
        rampart.utils.readdir(dir).forEach(function (f) {
            out.push({ name: f, text: totext.convertFile(dir + "/" + f) });
        });
        return out;                    /* the return value crosses back */
    },
    "/var/spool/incoming",
    function (docs) {                  /* on THIS thread's event loop */
        rampart.utils.printf("extracted %d documents\n", docs.length);
    }
);

/* this thread never blocked */
```

`rampart-server` is built on the same thing: it is multi-threaded by default, one
interpreter per thread, so CPU-bound request handlers do not queue behind each
other. Values move between threads through a clipboard
([`rampart.thread`](https://rampart.dev/docs/rampart-thread.html)), and
`rampart.event` triggers callbacks across them.

## Ship one file

Append a zip to the `rampart` executable and the result is a complete application in
a single file — scripts, native modules, static assets, HTML and all — that runs on a
machine with nothing installed on it.

```sh
cd mybundle && zip -qr ../payload.zip . && cd ..
cp /path/to/rampart myapp && cat payload.zip >> myapp && chmod +x myapp
```

The clever part is that nothing in your code has to know. A virtual `:zip:/`
namespace makes every file-reading API work unchanged, so `require()`,
`readFile()` and the web server's static-file handler all resolve inside the bundle
— the same source runs from disk during development and from inside the executable
in production. A bundle can also read its own appended payload
(`rampart.utils.payloadList`/`payloadGet`/`payloadExtract`), which is how the
Rampart installer unpacks itself. See
[Single-File Bundles](https://rampart.dev/docs/rampart-extras.html#single-file-bundles).

## What's inside

### Built into the executable

Always available, no `require()` needed.

| Global | What it does |
| --- | --- |
| [`rampart.utils`](https://rampart.dev/docs/rampart-utils.html) | A deep C-backed standard library: printf with ANSI color and HTML/URL/base64/JSON format codes, POSIX file and process I/O (`exec`, `fork`, `daemon`, `forkpty`, file watching), stat, glob, zip, gzip, hashing, URL parsing and resolution, date/timezone parsing, HyperLogLog, and a scriptable REPL |
| [`rampart.thread`](https://rampart.dev/docs/rampart-thread.html) | Real POSIX threads, each with its own JS interpreter and event loop. Share values through a clipboard with async callbacks — as easy as `setTimeout()`, but actually parallel |
| [`rampart.vector`](https://rampart.dev/docs/rampart-vector.html) | Typed vectors (f64/f32/f16/bf16/i8/u8/b8), conversion, L2 normalization and distance metrics |
| `rampart.event` | Cross-thread event registration and triggering |
| `rampart.import` | A fast CSV parser for bulk data migration |

ES2015+ syntax is handled by a built-in [tree-sitter transpiler](https://rampart.dev/docs/rampart-main.html#ecmascript-2015-with-transpiler)
(`"use transpiler"`) or by [Babel](https://rampart.dev/docs/rampart-main.html#ecmascript-2015-and-babel-js)
(`"use babel"`), both cached to disk after the first run.

`Intl` (ICU4C-backed) and a substantial subset of the
[WHATWG web platform APIs](https://rampart.dev/docs/rampart-main.html#whatwg-w3c-web-platform-apis-experimental)
— `fetch`, `URL`, `Blob`, streams, Web Crypto, `WebSocket` — come from
`rampart --install rampart-intl rampart-whatwg`. Once installed they need no
`require()`: the globals are built in and load their module on first reference,
so scripts that never touch them pay nothing at startup.

### rampart-sql — SQL, full text and vectors in one engine

[`rampart-sql`](https://rampart.dev/docs/sqltoc.html) embeds **Texis**, the relational
database and search engine from Thunderstone, in production behind
[large sites and government and corporate search](https://www.thunderstone.com/about-us/our-customers/)
for three decades. It is the reason Rampart has no separate search tier.

- **Metamorph full-text search** — suffix/prefix processing, thesaurus and linguistic
  derivations, phrase proximity, wildcards, concept-based natural-language queries and
  relevance ranking, all far beyond SQLite's FTS. Real-time indexing: `LIKEP` returns
  correct results even for rows the index hasn't caught up with yet.
- **Vector search** — `varvec` column types, `CREATE VECTOR INDEX` backed by
  [FAISS](https://github.com/facebookresearch/faiss) (IVFPQ) or
  [usearch](https://github.com/unum-cloud/usearch) (HNSW), and `LIKEV` similarity search
  over millions of vectors. Quantized i8/u8 indexes for compact storage.
- **Embeddings in SQL** — `embed()`, `chunkembed()`, `chunkavg()` and `chunkcoherence()`
  run llama.cpp, ONNX or CLIP models inside the engine, so rows are embedded without a
  round trip through JavaScript.
- **Hybrid retrieval** — keyword and vector candidates fused by Reciprocal Rank Fusion in
  a single statement, with per-retriever scores exposed.
- **And the rest of a real database** — joins, transactions, compound and inverted
  indexes, `STRLST`/`VARBYTE`/`INDIRECT`/`GEOCODE` types, geographic distance and
  bounded-area search, JSON path functions, `rex`/`re2` regex functions, parameterized
  queries, CSV import, and the `tsql` command-line client.

### rampart-langtools — local AI, no service required

[`rampart-langtools`](https://rampart.dev/docs/rampart-langtools.html) is seven modules
over best-in-class inference libraries, installed with
`rampart --install rampart-langtools` and usable directly or through `rampart-sql`.

| Module | What it does |
| --- | --- |
| `rampart-llamacpp` | GGUF models in-process via [llama.cpp](https://github.com/ggml-org/llama.cpp): text embedding, reranking, and text generation with streaming, chat templates, tool calling and reasoning separation. One shared, continuously-batched engine across threads. *note: [`rampart-llm`](https://rampart.dev/docs/rampart-extras.html#rampart-llm) is also available, to run inference in a separate `llama-server` process* |
| `rampart-onnx` | [ONNX Runtime](https://onnxruntime.ai/): embedding and reranking with the same handle API, plus a general session API for running *any* ONNX model. Automatic CPU/CUDA runtime selection |
| `rampart-clip` | CLIP image **and** text embeddings in one shared vector space — search a photo collection with a text query |
| `rampart-faiss` | Standalone FAISS indexes: factory strings, training, save/load, memory-mapped read-only search, optional GPU |
| `rampart-sentencepiece` | SentencePiece subword tokenization and detokenization |
| `rampart-ocr` | PP-OCR: text, box geometry and confidence from scans, multi-page TIFFs and screenshots, with layout-aware reading order |
| `rampart-models` | `models.get("bge-m3:q8_0")` returns a ready-to-use local path, fetching from HuggingFace on first use |

Structure-aware chunking (paragraph and sentence boundaries, not blind token windows),
per-chunk vectors with a document-level average and a coherence score, GPU acceleration
through Metal on Apple Silicon and CUDA on Linux, and model weights shared across threads
rather than duplicated.

> Langtools is available on every platform Rampart ships for. Two of its modules
> are not: `rampart-onnx` and `rampart-ocr` need glibc 2.28+, so they are absent
> from the glibc 2.17 builds and from 32-bit ARM. GPU acceleration is CUDA on
> Linux and Metal on Apple Silicon; the Intel Mac, FreeBSD and 32-bit ARM builds
> are CPU-only.

`rampart-llamacpp` carries its own build of llama.cpp. Embedding and reranking model
formats change slowly, so that build stays current for what it and `rampart-sql` are
there to do. Text generation is the fast-moving end: a brand-new LLM may want a
llama.cpp newer than the one in your Rampart, and a large one may belong on another
machine entirely. For both cases there is
[`rampart-llm`](https://rampart.dev/docs/rampart-extras.html#rampart-llm) — one
streaming API, one response shape, over `llama-server`, Ollama, any OpenAI-compatible
endpoint, Anthropic's API and the Claude Code CLI. Run `llama-server` on whatever
schedule and whichever host suits you; the calling code does not change.

### The rest of the modules

| Module | What it does |
| --- | --- |
| [`rampart-server`](https://rampart.dev/docs/rampart-server.html) | Multi-threaded HTTP/HTTPS server on libevhtp + libevent2. Static-file performance competitive with nginx, in the same process that runs your apps. WebSockets, chunked replies, deferred responses, hot module reload, an experimental reverse proxy, and a C API |
| [`rampart-auth`](https://rampart.dev/docs/rampart-auth.html)&nbsp;† | Authentication and sessions: protected paths, CSRF protection, CLI and HTTP administration, themes, and OAuth plugins |
| [`rampart-totext`](https://rampart.dev/docs/rampart-totext.html) | Plain text out of DOCX, PPTX, XLSX, ODT/ODP/ODS, EPUB, PDF, RTF, LaTeX, man pages, HTML, Markdown, email and mbox. Magic-byte detection, transparent gzip, OCR fallback for scanned pages |
| [`rampart-curl`](https://rampart.dev/docs/rampart-curl.html) | HTTP/HTTPS/FTP/SMTP/IMAP client with parallel async fetch and promise support |
| [`rampart-net`](https://rampart.dev/docs/rampart-net.html) | Async TCP/IP with TLS, Node-style sockets and servers, a WebSocket client, and DNS resolution |
| [`rampart-lmdb`](https://rampart.dev/docs/rampart-lmdb.html) | Fast, ACID, memory-mapped key-value store with cursors and transactions. Values are stored raw, with optional automatic JSON or CBOR encoding and decoding |
| [`rampart-redis`](https://rampart.dev/docs/rampart-redis.html)&nbsp;† | Redis client with async variants and an `XREAD` that behaves like PUB/SUB |
| [`rampart-crypto`](https://rampart.dev/docs/rampart-crypto.html) | OpenSSL: hashing, HMAC/KMAC, PBKDF2/HKDF/scrypt, RSA, EC, Ed25519/X25519, certificates — plus post-quantum ML-KEM and ML-DSA |
| [`rampart-html`](https://rampart.dev/docs/rampart-html.html) | jQuery-style server-side HTML traversal and manipulation, with error correction via HTMLTidy |
| [`rampart-gm`](https://rampart.dev/docs/rampart-gm.html)&nbsp;† | GraphicsMagick: resize, crop, convert, animated GIFs, thumbnails |
| [`rampart-python`](https://rampart.dev/docs/rampart-python.html)&nbsp;† | Embedded CPython driven from JavaScript, with automatic type conversion and a bundled `pip3r` |
| [`rampart-treesitter`](https://rampart.dev/docs/rampart-treesitter.html)&nbsp;† | Source parsing and symbol extraction across many languages |
| [`rampart-almanac`](https://rampart.dev/docs/rampart-almanac.html)&nbsp;† | Sun/moon/planet positions, seasons, holidays, and [weather forecasts](https://rampart.dev/weather/) |
| [`rampart-cmark`](https://rampart.dev/docs/rampart-cmark.html) | CommonMark and GitHub-flavored Markdown |
| [`rampart-robots`](https://rampart.dev/docs/rampart-robots.html)&nbsp;† | Google's robots.txt parser, for polite crawling |
| [`rampart-webview`](https://rampart.dev/docs/rampart-webview.html)&nbsp;† | Desktop apps: a native OS webview (WebKitGTK on Linux, WKWebView on macOS) driven from JavaScript — bind Rampart functions for the page to call, evaluate in the page, snapshot it. Includes a JavaScriptCore context for running browser JS libraries in-process |
| [`rampart-chromeview`](https://rampart.dev/docs/rampart-chromeview.html)&nbsp;† | A Puppeteer-compatible client for headless Chrome over CDP: navigate, wait on selectors, `$eval`, screenshots, PDF, request interception, and raw CDP sessions. Works with `puppeteer-extras` plugins |

† Fetched with `rampart --install`; the unmarked modules are in the base install.
(`rampart-gm` installs under the name `rampart-graphicsmagick`.)

And in [Extras](https://rampart.dev/docs/rampart-extras.html): `rampart-webserver` (the
standard server layout, Let's Encrypt integration, log rotation, monitoring),
`rampart-email` (direct MX, relay, SMTP or Gmail App Password delivery), `rampart-llm`
(the streaming LLM client described above), `rampart-cmodule` (compile C at runtime
into a JavaScript function), and the experimental
`rampart-nodeshim` (enough of Node's core modules to run many pure-JavaScript npm
packages). Of these, only `rampart-webserver` is in the base install.


## Why Rampart?

The usual stack is nginx, an app server, PostgreSQL, Elasticsearch and Redis: five or more
services, gigabytes of RAM, containers to wire together, and a dependency tree of a
thousand third-party packages to install, lock and audit.

Rampart is one install and one process, with a curated first-party module set and no
supply chain to worry about. Nearly every module is written in C, with JavaScript as the
orchestration layer on top — which is why the compact Duktape engine is the right choice
here rather than a RAM-hungry JIT. Setup is measured in minutes, and the whole thing is
small enough to leave room on the machine for whatever it was actually bought to do.

## Building from source

You need a C/C++ toolchain, `make`, and CMake 3.13 or newer. OpenSSL, libcurl,
libevent, LMDB, Python, ICU, FAISS, Texis and the rest are vendored in `extern/`
and built in tree, so the packages below are only what those vendored copies and
the optional modules want from the system.

**macOS** — the Xcode command line tools supply clang, make, flex, bison and perl.

```sh
brew install cmake libidn2 gawk python3 graphicsmagick openblas
# for a more complete rampart-python build:
brew install tcl-tk gdbm xz readline sqlite
```

**Debian / Ubuntu / Raspberry Pi OS**

```sh
apt install build-essential cmake git flex bison perl patchelf gfortran \
    libidn2-dev libldap2-dev python3 zlib1g-dev libopenblas-dev \
    libgraphicsmagick1-dev
# for a more complete rampart-python build:
apt install libsqlite3-dev uuid-dev tcl-dev tk-dev libgdbm-dev libbz2-dev \
    liblzma-dev libffi-dev libgdbm-compat-dev libncurses-dev libreadline-dev
```

**RHEL / CentOS / Fedora**

```sh
yum install epel-release
yum install gcc gcc-c++ make cmake git flex bison patchelf gcc-gfortran \
    perl perl-core perl-IPC-Cmd python3 \
    libidn2-devel openldap-devel zlib-devel openblas-devel GraphicsMagick-devel
# for a more complete rampart-python build:
yum install sqlite-devel tcl-devel tk-devel libuuid-devel readline-devel \
    ncurses-devel bzip2-devel gdbm-devel xz-devel libffi-devel
```

Two things are specific to the RHEL family. The base `perl` package is minimal and
omits modules the vendored OpenSSL's `Configure` uses, hence `perl-core` and
`perl-IPC-Cmd`. And RHEL strips static libraries, so `libidn2-devel` installs no
`libidn2.a` — which is what the vendored curl links against. If the curl step
cannot find it, build one:

```sh
curl -fsSL https://ftp.gnu.org/gnu/libidn/libidn2-2.3.7.tar.gz | tar xz
cd libidn2-2.3.7
./configure --prefix=/usr/local --enable-static --disable-shared \
    --with-included-libunistring
make && sudo make install
```

**Then:**

```sh
mkdir rampart/build
cd rampart/build
cmake ../
make
make install
```

Most files land in `/usr/local/rampart`, with links in `/usr/local/bin`. Note that
`rampart --install` works only on official builds, since the distribution packages are not
necessarily ABI-compatible with a self-compiled binary.

## Related projects

| Project | |
| --- | --- |
| [rampart-langtools](https://github.com/aflin/rampart-langtools) | AI embeddings, reranking, generation, OCR, CLIP and FAISS (via `rampart --install`) |
| [rampart-webview](https://github.com/aflin/rampart-webview) | Cross-platform desktop apps with native rendering (via `rampart --install`) |
| [rampart-iroh](https://github.com/aflin/rampart-iroh) | P2P networking with encrypted QUIC, pub/sub and blob transfer (via `rampart --install`) |
| [iroh-webproxy](https://github.com/aflin/iroh-webproxy) | Expose remote web servers locally through encrypted P2P tunnels. Installed by `rampart --install rampart-iroh`; `rampart-webserver` can start it with `--irohProxy` |
| [rampart_lang_derivs](https://github.com/aflin/rampart_lang_derivs) | Suffix matching rules for multilingual full-text search |
| [rampart_webdav](https://github.com/aflin/rampart_webdav) | WebDAV server with a web file manager, media playback and document editing |
| [rampart_webshield](https://github.com/aflin/rampart_webshield) | Text and image obfuscation to protect content from scraping |
| [Self_Hosted_Search_Engine](https://github.com/aflin/Self_Hosted_Search_Engine) | A personal search engine built from your browsing history |
| [rampart_wikipedia_search](https://github.com/aflin/rampart_wikipedia_search) | Keyword and semantic search across Wikipedia |
| [rampart_docs](https://github.com/aflin/rampart_docs) | Documentation source, with an `overview.md` written for LLM coding assistants |

## Documentation

Start with the [docs index](https://rampart.dev/docs/). The
[FAQ](https://rampart.dev/docs/faq.html) covers the questions people coming from Node.js
ask most, and the [tutorials](https://rampart.dev/docs/tutorialtoc.html) walk through
building real applications. The `web_server/` directory in this repository is a working
server layout you can copy as a starting point.

Building with an LLM coding assistant? Clone
[rampart_docs](https://github.com/aflin/rampart_docs) and point the assistant at
`source/overview.md` — it is written for exactly that.

## License

The core Rampart program and the Duktape engine are MIT licensed; see
[LICENSE](LICENSE). The `rampart-sql` module and the Texis library it embeds are governed
by the Rampart Source Available License; see [LICENSE-rsal.txt](LICENSE-rsal.txt).
Licenses for bundled third-party components are in [licenses/](licenses) and noted in the
acknowledgment section of each module's documentation.

## Questions

Reach out through the [help page](https://rampart.dev/help.html), or open an issue.

Rampart is built by Moat Crossing Systems LLC.
