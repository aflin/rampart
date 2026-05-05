# Vendored libomp (LLVM OpenMP runtime)

Source: https://github.com/llvm/llvm-project, tag `llvmorg-22.1.1`,
subdirectory `openmp/runtime/`.

License: Apache License 2.0 with LLVM Exception. See `LICENSE.txt`.

## Why vendored

Apple Clang does not ship the OpenMP runtime, and Homebrew's
`libomp.a` is built with `-mmacosx-version-min=15.0`, producing
50+ linker warnings per consumer when the rampart build targets
macOS 11.0. Vendoring lets us build it at our own deployment
target and removes the build-time dependency on Homebrew.

Used only on macOS — every other supported platform (Linux,
Linux 32-bit, FreeBSD 14, Windows MSYS2) uses its system OpenMP
via `find_package(OpenMP)`.

## What's vendored vs. dropped

**Kept (under `src/`):**
- All `.h` and `.inl` headers from `openmp/runtime/src/`.
- 31 `.cpp` source files needed for a Unix static build with no
  optional features (see `LIBOMP_CXX_SOURCES` in `CMakeLists.txt`).
- `z_Linux_asm.S` (only assembled file).
- `kmp_config.h.cmake` — configured at build time by our
  `CMakeLists.txt` into `kmp_config.h`.
- `include/omp.h.var`, `include/ompx.h.var` — configured into the
  consumer-facing `omp.h` / `ompx.h`.
- `i18n/en_US.txt` — source-of-truth for runtime message catalog.
- `kmp_i18n_id.inc` and `kmp_i18n_default.inc` — pre-generated
  from `i18n/en_US.txt` using the upstream
  `tools/message-converter.py` script. Regenerate only if
  `en_US.txt` is updated; that removes the build-time Python
  dependency.

**Dropped:**
- `doc/`, `test/`, `unittests/` — unused.
- `cmake/` — upstream's CMake helpers; replaced by our slim
  `CMakeLists.txt`.
- `tools/` — only needed for one-off i18n regeneration, run
  outside the normal build.
- `thirdparty/ittnotify/` — Intel ITT API; we build with
  `LIBOMP_USE_ITT_NOTIFY=0`.
- `ompt-general.cpp`, `ompt-specific.cpp`, `ompd-specific.cpp`,
  `kmp_debugger.cpp`, `kmp_stub.cpp`, `kmp_stats*.cpp` — disabled
  features (OMPT, OMPD, debugger, stub library, stats).
- Windows-specific files (`z_Windows_NT*.cpp`, `libomp.rc.var`).

## Disabled features (vs. Homebrew bottle)

`OMPT`, `OMPD`, `ITT`, `STATS`, `DEBUGGER`, `HWLOC`,
`USE_VERSION_SYMBOLS`, `USE_HIER_SCHED`, `INTERNODE_ALIGNMENT`.
None of these are used by FAISS, the only consumer in this
project. See `CMakeLists.txt` for the full feature flag list.

## Updating

To pull a new libomp version:

1. Download `openmp/runtime/` from the upstream tag
   (e.g. `llvmorg-23.x.y`) and replace files in `src/`.
2. Re-run `tools/message-converter.py` against `i18n/en_US.txt`
   to regenerate `kmp_i18n_id.inc` and `kmp_i18n_default.inc`.
3. Diff `src/kmp_config.h.cmake` against the previous version —
   any new `#cmakedefine` lines need a corresponding `set(...)`
   in `CMakeLists.txt`.
4. Rebuild and run `test/sql-vector-test.js` as a smoke test.
