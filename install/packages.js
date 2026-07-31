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
                "modules/rampart-open-meteo.js",
                /* Maintenance tool: rebuilds rampart-date-holidays.js
                   from the latest npm `date-holidays`.  A NODE script
                   (needs npm + esbuild), so it is NOT a rampart module
                   and never gets require()d -- it ships here so almanac
                   users can refresh holiday data themselves, and is
                   excluded from the core tarball. */
                "modules/node-update-date-holidays.js"],
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
                "bin/pip3r",              /* self-locating wrapper script (src/pip3r) */
                "bin/python3r"],          /* self-locating wrapper script (src/python3r) */
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
        /* CPU (universal) langtools.  Per-platform naming:
             tiered linux (x86_64/arm64): _cpu suffix
             linux-2_28-armv7l:           _arm6 -- the ARMv6 build is the
                                          default here because ONE armv7
                                          package serves every 32-bit Pi,
                                          and only armv6 runs on all of
                                          them (Pi Zero/1 = armv6, Pi 2 =
                                          armv7-a, Pi 3+ = armv8).  The
                                          faster armv8-a build ships as
                                          the separate
                                          `rampart-langtools-arm8a`
                                          variant, exactly like the cuNN
                                          variants: install it explicitly
                                          and it re-points the symlinks.
             mac/freebsd/raspi (legacy):  plain unsuffixed
           build-packages.js skips files that aren't present, and a
           symlink value may be an ARRAY of candidate targets (first one
           staged wins) -- so the union of every naming form is safe
           here and one entry serves all platforms. */
        files: ["modules/rampart-faiss_cpu.so",
                "modules/rampart-faiss_arm6.so",
                "modules/rampart-faiss.so",
                "modules/rampart-llamacpp_cpu.so",
                "modules/rampart-llamacpp_arm6.so",
                "modules/rampart-llamacpp.so",
                "modules/rampart-clip_cpu.so",
                "modules/rampart-clip_arm6.so",
                "modules/rampart-clip.so",
                "modules/rampart-sentencepiece.so",
                /* rampart-onnx: ONE CPU-floor module (static ONNX
                   Runtime) + its JS model catalog.  Not variant-
                   specific -- ships in every langtools package.  GPU
                   acceleration comes from the sibling onnx-cuNN/ dirs
                   added to the cu12/cu13 packages, which rampart-onnx.so
                   auto-discovers by its own location.  Not built on the
                   2_17 tier or armv7; skip-missing drops it there. */
                "modules/rampart-onnx.so",
                "modules/rampart-models.js",
                /* rampart-clip's test + its Public-Domain photo corpus.
                   Shipped in EVERY langtools variant (same rule as
                   sentencepiece/onnx) so a lone --install of any one
                   variant is self-contained and the test is runnable.
                   Skip-missing drops them where clip isn't built. */
                "test/clip-test.js",
                "test/test_images/"],
        /* first candidate that got staged wins: _cpu on the tiered linux
           builds, _arm6 on armv7, and neither on mac/freebsd/legacy-raspi
           (where the plain unsuffixed file stays canonical). */
        symlinks: {
            "modules/rampart-faiss.so":    ["rampart-faiss_cpu.so",
                                            "rampart-faiss_arm6.so"],
            "modules/rampart-llamacpp.so": ["rampart-llamacpp_cpu.so",
                                            "rampart-llamacpp_arm6.so"],
            "modules/rampart-clip.so":     ["rampart-clip_cpu.so",
                                            "rampart-clip_arm6.so"]
        },
        notes: "Local LLM + vector search + CLIP + ONNX embeddings (CPU build)"
    },

    /* armv8-a langtools for 32-bit ARM -- the opt-in speed upgrade over
       the armv6 build the default package installs.  Same shape as the
       cuNN variants: it ships its own suffixed .so files plus symlinks
       that re-point the unsuffixed names at them, and `--install all`
       skips it so the safe default is never silently replaced.
       ONLY for a Pi 3 or newer (Cortex-A53/A72/A76 -- `grep Features
       /proc/cpuinfo` shows `crc32`).  On a Pi Zero/1 (armv6) or a Pi 2
       (Cortex-A7, armv7-a) these modules will die with SIGILL/"Illegal
       instruction" the first time a kernel runs.  Built only on the
       armv7 platform, so `--install` on any other platform simply finds
       no such package. */
    "rampart-langtools-arm8a": {
        kind:  "tar.gz",
        arch:  "dep",
        files: ["modules/rampart-faiss_arm8a.so",
                "modules/rampart-llamacpp_arm8a.so",
                "modules/rampart-clip_arm8a.so",
                "modules/rampart-sentencepiece.so",
                "modules/rampart-models.js",
                "test/clip-test.js",
                "test/test_images/"],
        platforms: /^linux-[^-]+-armv7l$/,
        symlinks: {
            "modules/rampart-faiss.so":    "rampart-faiss_arm8a.so",
            "modules/rampart-llamacpp.so": "rampart-llamacpp_arm8a.so",
            "modules/rampart-clip.so":     "rampart-clip_arm8a.so"
        },
        notes: "Langtools built for armv8-a (Pi 3+ only; default package is armv6)"
    },

    /* CUDA-accelerated langtools.  Three per-runtime variants -- pick
       the one matching the libcudart.so.<N> already on the host.
       Built only for linux-*-x86_64 tiers; the `platforms` regex on each
       keeps them out of --list and refuses them on other platforms. */
    "rampart-langtools-cu11": {
        kind:  "tar.gz",
        arch:  "dep",
        /* onnx has no cu11 GPU runtime -- cu11 carries the CPU-floor
           rampart-onnx.so (+ models.js) so a lone --install of this
           variant is self-contained; onnx just runs on CPU here. */
        files: ["modules/rampart-faiss_cu11.so",
                "modules/rampart-llamacpp_cu11.so",
                "modules/rampart-clip_cu11.so",
                "modules/rampart-sentencepiece.so",
                "modules/rampart-onnx.so",
                "modules/rampart-models.js",
                "test/clip-test.js",
                "test/test_images/"],
        /* x86_64 ONLY -- unlike cu12/cu13 there is no ARM cu11.  The ARM
           CUDA-11 build would bake SASS for sm_72/sm_87 (Xavier/Orin --
           Jetson iGPUs) while being compiled against CUDA 11.8, and no
           JetPack ever shipped 11.8 (JP4=10.2, JP5=11.4, JP6=12.6 ->
           cu12, JP7=13 -> cu13), so its driver floor can never be met on
           the only hardware it targets.  ARM GPU boxes use cu12/cu13. */
        platforms: /^linux-[^-]+-x86_64$/,
        symlinks: {
            "modules/rampart-faiss.so":    "rampart-faiss_cu11.so",
            "modules/rampart-llamacpp.so": "rampart-llamacpp_cu11.so",
            "modules/rampart-clip.so":     "rampart-clip_cu11.so"
        },
        notes: "Langtools, CUDA 11 faiss/llamacpp/clip; onnx CPU (linux-*-x86_64 only)"
    },

    "rampart-langtools-cu12": {
        kind:  "tar.gz",
        arch:  "dep",
        /* GPU onnx: the sibling modules/onnx-cu12/ runtime dir ships
           verbatim (ORT libs + sm.list + internal symlinks; trailing
           slash -> cp -a).  rampart-onnx.so auto-discovers it.  NO
           NVIDIA-authored CUDA libs are bundled -- GPU onnx requires a
           system CUDA 12 toolkit + cuDNN 9.  Skip-missing drops the dir
           on tiers that didn't build it. */
        files: ["modules/rampart-faiss_cu12.so",
                "modules/rampart-llamacpp_cu12.so",
                "modules/rampart-clip_cu12.so",
                "modules/rampart-sentencepiece.so",
                "modules/rampart-onnx.so",
                "modules/rampart-models.js",
                "modules/onnx-cu12/",
                "test/clip-test.js",
                "test/test_images/"],
        /* 2_28 ONLY.  `[^-]+` would also match the 2_17 tier, which
           never builds cu12/cu13 -- and because sentencepiece/models.js/
           the clip test fixtures DO exist there, build-packages.js's
           any-file-present check would happily emit a ~600 KB tarball
           with no CUDA modules in it at all.  Installing that would
           strip the unsuffixed langtools symlinks (preExtract removes
           them; the tarball has none to restore) and leave a working
           CPU install broken.  On arm64 every CUDA variant is 2_28-only. */
        platforms: /^linux-2_28-(x86_64|arm64)$/,
        symlinks: {
            "modules/rampart-faiss.so":    "rampart-faiss_cu12.so",
            "modules/rampart-llamacpp.so": "rampart-llamacpp_cu12.so",
            "modules/rampart-clip.so":     "rampart-clip_cu12.so"
        },
        notes: "Langtools, CUDA 12 (faiss/llamacpp/clip + onnx GPU) (linux-*-x86_64 only)"
    },

    "rampart-langtools-cu13": {
        kind:  "tar.gz",
        arch:  "dep",
        /* GPU onnx: sibling modules/onnx-cu13/ dir ships verbatim; no
           NVIDIA CUDA libs bundled (needs system CUDA 13 + cuDNN 9).
           See the cu12 entry. */
        files: ["modules/rampart-faiss_cu13.so",
                "modules/rampart-llamacpp_cu13.so",
                "modules/rampart-clip_cu13.so",
                "modules/rampart-sentencepiece.so",
                "modules/rampart-onnx.so",
                "modules/rampart-models.js",
                "modules/onnx-cu13/",
                "test/clip-test.js",
                "test/test_images/"],
        /* 2_28 ONLY -- see the cu12 entry for why `[^-]+` is wrong here. */
        platforms: /^linux-2_28-(x86_64|arm64)$/,
        symlinks: {
            "modules/rampart-faiss.so":    "rampart-faiss_cu13.so",
            "modules/rampart-llamacpp.so": "rampart-llamacpp_cu13.so",
            "modules/rampart-clip.so":     "rampart-clip_cu13.so"
        },
        notes: "Langtools, CUDA 13 (faiss/llamacpp/clip + onnx GPU) (linux-*-x86_64 only)"
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
