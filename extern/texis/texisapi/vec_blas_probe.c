/* vec_blas_probe.c — see vec_blas_probe.h for rationale.
 *
 * Per-platform behavior:
 *
 * Linux: probe three libs in order — libgomp.so.1 (OpenMP, openblas
 *   internally references GOMP_*), libgfortran.so.5 (transitive dep of
 *   libopenblas via its LAPACK Fortran routines — dlopen first so the
 *   error message names libgfortran specifically if that one's missing),
 *   then libopenblas.so.0 itself.
 *
 * FreeBSD: only libopenblas.so.0 is missing on a vanilla install.
 *   libomp.so is FreeBSD base (LLVM is the base toolchain), and
 *   libgfortran.so.5 + libquadmath.so.0 ride in via libopenblas's own
 *   DT_NEEDED chain — if libopenblas loads, those loaded too.  One probe
 *   call, one install hint ("pkg install openblas" pulls the gcc14 chain
 *   transitively).
 *
 * Other platforms: gate in vecindex.c never calls this, but build a
 *   defensive always-pass body so static analyzers and unit-test rigs
 *   see consistent behavior.
 *
 * All dlopen calls use RTLD_GLOBAL so the symbols become visible to
 * rampart-sql.so's deferred (unresolved) references.  Handles are
 * intentionally leaked (process-lifetime).
 */
#include "vec_blas_probe.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>

/* rampart's main binary tracks its own install prefix (e.g.
   "/home/aaron/.rampart").  Defined in src/cmdline.c.  We try to dlopen
   from <rampart_dir>/lib/ BEFORE falling back to a bare-SONAME dlopen,
   because some rtld variants (notably FreeBSD's) don't propagate a
   calling .so's DT_RUNPATH to dlopen calls -- so even though
   rampart-sql.so has RPATH "$ORIGIN/../lib", a bare dlopen() from
   inside it won't find the bundled libs.
   __attribute__((weak)) so that texis-side CLI tools (tsql, texislockd,
   rbtest, etc.) that link libtexisapi.a but NOT cmdline.o still link
   cleanly: the weak symbol resolves to NULL and probe_dlopen falls
   straight through to the bare-SONAME path. */
__attribute__((weak)) extern char rampart_dir[];

static int        g_probed = 0;
static int        g_ok     = 0;
static const char *g_err   = NULL;

static void *probe_dlopen(const char *soname)
{
    char path[PATH_MAX];
    void *h;
    /* Test the symbol's address against NULL (weak-undefined sentinel)
       before reading rampart_dir[0] -- otherwise tsql/texislockd would
       segfault here. */
    if (&rampart_dir[0] && rampart_dir[0]) {
        snprintf(path, sizeof path, "%s/lib/%s", rampart_dir, soname);
        h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
        if (h) return h;
    }
    /* Fall back to bare SONAME so that a system-installed copy still
       satisfies the probe even if <prefix>/lib/ doesn't have it. */
    return dlopen(soname, RTLD_NOW | RTLD_GLOBAL);
}

int texis_vec_blas_probe(const char **err_out)
{
    if (!g_probed) {
        g_probed = 1;

#if defined(__linux__)
        if (!probe_dlopen("libgomp.so.1")) {
            g_err = "INDEX_VEC backend=ivfpq requires libgomp.so.1 "
                    "(OpenMP runtime).  Debian/Ubuntu: "
                    "sudo apt install libgomp1";
            goto done;
        }
        if (!probe_dlopen("libgfortran.so.5")) {
            g_err = "INDEX_VEC backend=ivfpq requires libgfortran.so.5.  "
                    "Debian/Ubuntu: sudo apt install libgfortran5";
            goto done;
        }
        if (!probe_dlopen("libopenblas.so.0")) {
            g_err = "INDEX_VEC backend=ivfpq requires libopenblas.so.0.  "
                    "Debian/Ubuntu: sudo apt install libopenblas0-pthread";
            goto done;
        }
#elif defined(__FreeBSD__)
        /* libomp.so is base; libgfortran+libquadmath ride in via
         * libopenblas's own DT_NEEDED chain. */
        if (!probe_dlopen("libopenblas.so.0")) {
            g_err = "INDEX_VEC backend=ivfpq requires libopenblas.so.0.  "
                    "FreeBSD: sudo pkg install openblas";
            goto done;
        }
#else
        /* Other platforms: probe always succeeds — the gate in
         * vecindex.c never calls us, so this branch is unreachable in
         * practice. */
#endif
        g_ok = 1;
    }
done:
    if (err_out) *err_out = g_err;
    return g_ok;
}
