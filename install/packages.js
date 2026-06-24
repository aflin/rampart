/*
 * install/packages.js -- the single source of truth for what rampart
 * distributes.  Consumed by both:
 *
 *   - mkrp              : walks the list, builds the right artifact
 *                         (single .so / .js / .tar.gz) for each entry,
 *                         sha1sum's it, and uploads to:
 *                           rampart.dev/downloads/rampart-<ver>/<plat>/packages/
 *
 *   - rampart --install  : reads the entry for the requested package and
 *                         either downloads a single file or downloads +
 *                         extracts a tarball under <prefix>/.
 *                         The runtime gets <plat> from
 *                         rampart.buildPlatform.split(";")[0].trim() --
 *                         which means `rampart --install` MUST refuse
 *                         to run unless buildPlatform carries an
 *                         official "NAME;..." prefix (i.e. the binary
 *                         came from a build that was registered for
 *                         distribution).  A vanilla self-build whose
 *                         buildPlatform is bare `uname -a` won't have
 *                         matching artifacts on the server.
 *
 *
 * SCHEMA (per entry, keyed by package name)
 * -----------------------------------------
 * {
 *     in_bundle : true                // (1)
 *
 *     kind      : "so" | "js" | "tar.gz"   // single-file or archive
 *     arch      : "dep" | "indep"          // does file content vary by
 *                                          // build platform?
 *     files     : [ <path>, ... ]          // (2)
 *     symlinks  : { <linkpath>: <target> } // optional, for tar.gz
 *     deps      : [ <pkgname>, ... ]       // optional; documentation
 *                                          // only -- no auto-resolve
 *                                          // until later
 *     notes     : "..."                    // optional free-text
 * }
 *
 *  (1) in_bundle entries are listed for completeness so the build script
 *      and the runtime both know "this is real, already present, don't
 *      try to fetch it".  No other fields are required.
 *
 *  (2) Paths are relative to <prefix>/ (which is /usr/local/rampart on
 *      official installs).  A trailing `/` means "the whole subtree".
 *      Symlinks present in the source tree are archived as symlinks
 *      (tar's default behavior).
 *
 *
 * ARTIFACT NAMING ON THE SERVER
 * -----------------------------
 *   <plat>/packages/<pkg>-<ver>-<plat>.{so,tar.gz}    (arch: "dep")
 *   <plat>/packages/<pkg>-<ver>.js                    (arch: "indep")
 *   <plat>/packages/<pkg>-<ver>.tar.gz                (arch: "indep")
 *   <plat>/packages/<file>.sha1                       (one per artifact)
 *
 * The runtime tries .so, .js, .tar.gz in that order if the manifest
 * entry's `kind` is missing -- but the official channel always sets
 * `kind` so this fallback only matters for community-added packages.
 */

module.exports = {

    /* =============================================================
     * Packages shipped inside the installer bundle.  No separate
     * artifact is produced; rampart --install treats these as
     * "already installed" and silently skips them.
     * ============================================================= */

    "rampart-cmark":      { in_bundle: true },
    "rampart-crypto":     { in_bundle: true },
    "rampart-curl":       { in_bundle: true },
    "rampart-html":       { in_bundle: true },
    "rampart-lmdb":       { in_bundle: true },
    "rampart-net":        { in_bundle: true,
                            notes: "internally loads rampart-crypto" },
    "rampart-server":     { in_bundle: true,
                            notes: "internally loads rampart-crypto" },
    "rampart-sql":        { in_bundle: true,
                            notes: "soft-requires rampart-llamacpp only" +
                                   " for vector-embedding SQL paths;" +
                                   " throws when used without it" },
    "rampart-totext":     { in_bundle: true,
                            notes: "embeds require('rampart-cmark')" +
                                   " and require('rampart-html');" +
                                   " both bundled" },
    "rampart-sqlUpdate":  { in_bundle: true },
    "rampart-webserver":  { in_bundle: true },
    "babel":              { in_bundle: true },
    "babel-polyfill":     { in_bundle: true },

    /* =============================================================
     * Single-file .so packages (arch-dependent)
     * ============================================================= */

    "rampart-redis": {
        kind:  "so",
        arch:  "dep",
        files: ["modules/rampart-redis.so"],
        notes: "Redis client"
    },

    "rampart-robots": {
        kind:  "so",
        arch:  "dep",
        files: ["modules/rampart-robots.so"],
        notes: "robots.txt parser"
    },

    "rampart-auth": {
        kind:  "so",
        arch:  "dep",
        files: ["modules/rampart-auth.so"],
        deps:  ["rampart-server"],
        notes: "HTTP auth helpers for rampart-server"
    },

    "rampart-whatwg": {
        kind:  "so",
        arch:  "dep",
        files: ["modules/rampart-whatwg.so"],
        notes: "WHATWG globals (Blob, File, URL, fetch, etc.)"
    },

    "rampart-intl": {
        kind:  "so",
        arch:  "dep",
        files: ["modules/rampart-intl.so"],
        notes: "Intl/ICU globals (Collator, DateTimeFormat, ...)"
    },

    "rampart-treesitter": {
        kind:  "so",
        arch:  "dep",
        files: ["modules/rampart-treesitter.so"],
        notes: "Tree-sitter language parsers (syntax, ASTs)"
    },

    /* =============================================================
     * Single-file .js packages (arch-independent)
     * ============================================================= */

    "rampart-email": {
        kind:  "js",
        arch:  "indep",
        files: ["modules/rampart-email.js"],
        deps:  ["rampart-curl", "rampart-net"],
        notes: "SMTP send helper, with built-in Gmail support"
    },

    "rampart-llm": {
        kind:  "js",
        arch:  "indep",
        files: ["modules/rampart-llm.js"],
        deps:  ["rampart-curl"],
        notes: "LLM client (llamacpp, Ollama, OpenAI, Anthropic)"
    },

    "rampart-chromeview": {
        kind:  "js",
        arch:  "indep",
        files: ["modules/rampart-chromeview.js"],
        deps:  ["rampart-curl", "rampart-net"],
        notes: "Puppeteer-compatible headless Chrome control"
    },

    /* =============================================================
     * Tarball packages (arch-dependent)
     * ============================================================= */

    "rampart-almanac": {
        kind:  "tar.gz",
        arch:  "dep",
        files: ["modules/rampart-almanac.so",
                "modules/rampart-date-holidays.js",
                "modules/rampart-open-meteo.js"],
        deps:  ["rampart-curl", "rampart-lmdb"],
        notes: "Sun/moon/planet, holidays, Open-Meteo weather"
    },

    "rampart-graphicsmagick": {
        kind:  "tar.gz",
        arch:  "dep",
        files: ["modules/rampart-graphicsmagick.so",
                "modules/rampart-gm.js"],
        /* No bundle_so_deps / bundle_gm_config: requires a system
           GraphicsMagick (brew/apt/pkg).  Bundling libGraphicsMagick
           transitively dragged in libheif -> libx265 (GPL-2-or-later),
           which can't be redistributed alongside the proprietary
           rampart-sql in good conscience.  Until that's resolved
           (Apple ImageIO shim, libheif-without-x265 rebuild, etc.),
           rampart-gm.js prints platform-specific install instructions
           on dlopen failure.  See claude-work/rampart-gm-bundling-backup/
           for the bundling code we removed. */
        notes: "Image processing (GraphicsMagick Wand API; requires system GraphicsMagick)"
    },

    "rampart-iroh": {
        kind:  "tar.gz",
        arch:  "dep",
        files: ["modules/rampart-iroh.so",
                "bin/iroh-webproxy"],
        notes: "Iroh p2p: docs, blobs, gossip + iroh-webproxy"
    },

    "rampart-webview": {
        kind:  "tar.gz",
        arch:  "dep",
        files: ["modules/rampart-webview.so"],
        notes: "Native webview (webview/webview) + JavaScriptCore"
    },

    "rampart-nodeshim": {
        kind:  "tar.gz",
        arch:  "dep",
        files: ["modules/rampart-nodeshim.so",
                /* core node-shim modules (all relative to <prefix>/) */
                "modules/assert.js",
                "modules/async_hooks.js",
                "modules/buffer.js",
                "modules/child_process.js",
                "modules/console.js",
                "modules/constants.js",
                "modules/crypto.js",
                "modules/diagnostics_channel.js",
                "modules/dns.js",
                "modules/events.js",
                "modules/fs.js",
                "modules/http.js",
                "modules/http2.js",
                "modules/https.js",
                "modules/module.js",
                "modules/net.js",
                "modules/os.js",
                "modules/path.js",
                "modules/perf_hooks.js",
                "modules/process.js",
                "modules/punycode.js",
                "modules/puppeteer-extras.js",
                "modules/querystring.js",
                "modules/readline.js",
                "modules/repl.js",
                "modules/stream.js",
                "modules/string_decoder.js",
                "modules/timers.js",
                "modules/tls.js",
                "modules/tty.js",
                "modules/url.js",
                "modules/util.js",
                "modules/v8.js",
                "modules/vm.js",
                "modules/worker_threads.js",
                "modules/zlib.js",
                /* helper-subdir trees */
                "modules/assert/",
                "modules/dns/",
                "modules/fs/",
                "modules/path/",
                "modules/stream/",
                "modules/timers/",
                "modules/util/"],
        notes: "Node.js compatibility layer (fs/path/events/...)"
    },

    "rampart-python": {
        kind:  "tar.gz",
        arch:  "dep",
        files: ["modules/rampart-python.so",
                "modules/python/",        /* entire Python 3.11 runtime */
                "bin/pip3r",              /* symlink to python/bin/pip3 */
                "bin/python3r"],          /* symlink to python/bin/python3 */
        symlinks: {
            /* rampart-python.c hard-codes PYTHONHOME=<modules_dir>/python3-lib;
               on the build host that's a symlink into python/lib/python3.11.
               Recreate it on the install target. */
            "modules/python3-lib": "python/lib/python3.11"
        },
        notes: "Embedded Python 3.11 runtime + pip"
    },

    "rampart-langtools": {
        kind:  "tar.gz",
        arch:  "dep",
        /* CPU (universal) langtools.  Tiered linux builds ship the
           _cpu variants; mac/freebsd/pi build the plain
           rampart-llamacpp.so / rampart-faiss.so directly.  Both
           naming forms are listed so build-packages.js can pick up
           whichever the platform actually produced (missing files
           are skipped; symlinks whose target wasn't staged are
           skipped). */
        files: ["modules/rampart-faiss_cpu.so",
                "modules/rampart-faiss.so",
                "modules/rampart-llamacpp_cpu.so",
                "modules/rampart-llamacpp.so",
                "modules/rampart-sentencepiece.so"],
        symlinks: {
            "modules/rampart-faiss.so":    "rampart-faiss_cpu.so",
            "modules/rampart-llamacpp.so": "rampart-llamacpp_cpu.so"
        },
        notes: "Local LLM + vector search (llamacpp, faiss); CPU build"
    },

    /* CUDA-accelerated langtools.  Three per-runtime variants -- pick
       the one matching the libcudart.so.<N> already on the host.
       Built only for linux-*-x86_64 tiers; install-side resolver
       refuses these on other platforms. */
    "rampart-langtools-cu11": {
        kind:  "tar.gz",
        arch:  "dep",
        files: ["modules/rampart-faiss_cu11.so",
                "modules/rampart-llamacpp_cu11.so",
                "modules/rampart-sentencepiece.so"],
        symlinks: {
            "modules/rampart-faiss.so":    "rampart-faiss_cu11.so",
            "modules/rampart-llamacpp.so": "rampart-llamacpp_cu11.so"
        },
        notes: "Langtools, CUDA 11 (linux-*-x86_64 only)"
    },

    "rampart-langtools-cu12": {
        kind:  "tar.gz",
        arch:  "dep",
        files: ["modules/rampart-faiss_cu12.so",
                "modules/rampart-llamacpp_cu12.so",
                "modules/rampart-sentencepiece.so"],
        symlinks: {
            "modules/rampart-faiss.so":    "rampart-faiss_cu12.so",
            "modules/rampart-llamacpp.so": "rampart-llamacpp_cu12.so"
        },
        notes: "Langtools, CUDA 12 (linux-*-x86_64 only)"
    },

    "rampart-langtools-cu13": {
        kind:  "tar.gz",
        arch:  "dep",
        files: ["modules/rampart-faiss_cu13.so",
                "modules/rampart-llamacpp_cu13.so",
                "modules/rampart-sentencepiece.so"],
        symlinks: {
            "modules/rampart-faiss.so":    "rampart-faiss_cu13.so",
            "modules/rampart-llamacpp.so": "rampart-llamacpp_cu13.so"
        },
        notes: "Langtools, CUDA 13 (linux-*-x86_64 only)"
    },

    /* =============================================================
     * Tarball packages (arch-independent)
     * ============================================================= */

    "rampart-cmodule": {
        kind:  "tar.gz",
        arch:  "indep",
        files: ["modules/rampart-cmodule.js",
                "include/"],                /* rampart.h, duktape.h, etc. */
        notes: "Requires a host C toolchain to build user modules"
    },

    /* =============================================================
     * Resource-only packages (no module; just trees of files)
     * ============================================================= */

    "web-server": {
        kind:  "tar.gz",
        arch:  "indep",
        files: ["web_server/"],
        notes: "Starter scaffolding for serverRoot"
    },

    "unsupported-extras": {
        kind:  "tar.gz",
        arch:  "indep",
        files: ["unsupported_extras/"],
        notes: "Includes c_module_template_maker (needs rampart-cmodule)" +
               " and llm-demo (needs rampart-langtools)"
    },

    "test": {
        kind:  "tar.gz",
        arch:  "indep",
        files: ["run_tests.sh", "test/"],
        notes: "rampart self-test suite"
    },

    "derivations": {
        kind:  "tar.gz",
        arch:  "indep",
        files: ["derivations/"],
        notes: "Language suffix-processing data for rampart-sql"
    }
};
