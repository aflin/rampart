#!/bin/bash
# docker/build-in-oven.sh <stage> -- runs INSIDE the manylinux2014 container.
# Do not run this on the host; invoke docker/build.sh instead.
#
# Stages (each is a separate `docker run`, sharing build/manylinux2014/):
#   build    bake the centos7 platform label, configure + compile into
#            build/manylinux2014/ (runs as root; hands the dir back to you)
#   test     run build/manylinux2014/src/run_tests.sh (runs as the invoking
#            user so run_tests.sh doesn't hit its "running as root" prompt)
#   install  replace /usr/local/rampart-ml with this build (runs as root)
set -euo pipefail

STAGE="${1:-build}"

SRC=/src
BUILD=$SRC/build/${RAMPART_BUILDDIR:-manylinux2014}
# install target; build.sh mounts this same host dir at this same path and passes
# it in, so cmake installs to (and records) the real dir, not a /usr/local/rampart-ml remap.
PREFIX="${RAMPART_PREFIX:-/usr/local/rampart-ml}"

enable_toolchain() {
    # modern gcc on old glibc; also sets LD_LIBRARY_PATH for libgomp/gfortran.
    # The devtoolset 'enable' script references unset vars (MANPATH, etc.), so
    # relax nounset around the source (the rest of the script runs with -u).
    set +u
    # devtoolset (manylinux2014) OR gcc-toolset (manylinux_2_28); pick the newest.
    sc=$(ls /opt/rh/gcc-toolset-*/enable /opt/rh/devtoolset-*/enable 2>/dev/null | sort -V | tail -1) || true
    [ -n "$sc" ] && source "$sc"
    set -u
}

case "$STAGE" in
  build)
    # bake the platform label into THIS build (container-local file only; the
    # host's /usr/local/rampart-build is never mounted, so it is untouched).
    # Format: linux-<tier>-<arch>; <uname -srvm>.  uname -srvm (not -a) drops the
    # random container hostname; the kernel version string still names the build
    # host (Debian).  Tier = the build base's glibc floor (2_17 = manylinux2014,
    # 2_28 = manylinux_2_28); arch label is arm64 (not aarch64) on ARM.  Read at
    # build time by src/CMakeLists.txt -> RAMPART_BUILD_PLATFORM.
    case "${RAMPART_BUILDDIR:-manylinux2014}" in *2_28*) _tier=2_28 ;; *) _tier=2_17 ;; esac
    case "$(uname -m)" in aarch64) _arch=arm64 ;; *) _arch=$(uname -m) ;; esac
    printf 'linux-%s-%s; %s\n' "$_tier" "$_arch" "$(uname -srvm)" > /usr/local/rampart-build
    echo "==> rampart.buildPlatform will be: $(cat /usr/local/rampart-build)"

    enable_toolchain
    echo "==> toolchain: $(gcc --version | head -1)"
    command -v gfortran >/dev/null || echo "WARNING: gfortran not found (OpenBLAS needs it)"

    # repo is owned by the invoking user but this stage runs as root; let git
    # operations (version detection, etc.) trust the bind-mounted tree.
    git config --global --add safe.directory '*' 2>/dev/null || true

    CMAKE=$(command -v cmake || command -v cmake3)
    mkdir -p "$BUILD"
    "$CMAKE" -S "$SRC" -B "$BUILD" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX"
    "$CMAKE" --build "$BUILD" -j"$(nproc)"

    # hand the build dir back to the invoking user (no root-owned droppings)
    if [ -n "${HOST_UID:-}" ] && [ -n "${HOST_GID:-}" ]; then
        chown -R "${HOST_UID}:${HOST_GID}" "$BUILD"
    fi
    echo
    "$BUILD/src/rampart" -c 'console.log("==> build OK. buildPlatform: " + rampart.buildPlatform)' || true
    ;;

  test)
    # run_tests.sh prompts when run as root; build.sh runs this stage as the
    # invoking (non-root) user, so it proceeds without prompting.
    if [ ! -x "$BUILD/src/run_tests.sh" ]; then
        echo "no build found at $BUILD/src -- run the 'build' stage first" >&2
        exit 1
    fi
    enable_toolchain    # runtime libs (libgomp/gfortran/openblas) for rampart-sql etc.
    cd "$BUILD/src"
    ./run_tests.sh
    ;;

  install)
    if [ ! -d "$BUILD" ]; then
        echo "no build found at $BUILD -- run the 'build' stage first" >&2
        exit 1
    fi
    enable_toolchain
    CMAKE=$(command -v cmake || command -v cmake3)
    # rampart bakes absolute install paths from the build-time CMAKE_INSTALL_PREFIX,
    # so the install dir is whatever `build` configured -- `--install --prefix` can
    # only move RELATIVE rules, not those.  Verify the build matches this target.
    cfg=$(sed -n 's/^CMAKE_INSTALL_PREFIX:[^=]*=//p' "$BUILD/CMakeCache.txt" 2>/dev/null)
    if [ "$cfg" != "$PREFIX" ]; then
        echo "build was configured to install into '${cfg:-?}', not '$PREFIX' --" >&2
        echo "re-run 'build' with the same -b/-d, then 'install'." >&2
        exit 1
    fi
    echo "==> installing into $PREFIX (replacing existing contents)"
    mkdir -p "$PREFIX"
    rm -rf "${PREFIX:?}/"*
    "$CMAKE" --install "$BUILD"

    # Stage the oven's glibc<=2.17 BLAS chain into <prefix>/lib so a host-side
    # `mkrp bundle` / build-installer.sh bundles THESE (centos7) instead of the
    # host's newer (glibc 2.34) system copies -- build-installer.sh prefers
    # $INSTALLED/lib.  rampart-sql.so finds libopenblas here at runtime via its
    # own $ORIGIN/../lib RPATH; but libopenblas in turn NEEDs libgfortran/libgomp
    # and has no RPATH of its own, so we patchelf each staged lib's RPATH to
    # '$ORIGIN' (same as build-installer.sh does for the bundle) -- otherwise the
    # chain only resolves where the SYSTEM already ships it (e.g. Debian) and
    # IVFPQ fails on a clean CentOS 7.  patchelf is in the oven image.
    echo "==> staging glibc<=2.17 BLAS chain into $PREFIX/lib"
    mkdir -p "$PREFIX/lib"
    for L in libopenblas.so.0 libgfortran.so.5 libgomp.so.1 libquadmath.so.0; do
        src=$(ldconfig -p 2>/dev/null | awk -v l="$L" '$1==l{r=$NF} END{print r}')
        if [ -n "$src" ] && [ -e "$src" ]; then
            cp -Lf "$src" "$PREFIX/lib/$L"
            patchelf --set-rpath '$ORIGIN' "$PREFIX/lib/$L" \
                || echo "    WARNING: patchelf failed on $L (IVFPQ may then need a system $L)"
            echo "    staged $L <- $src"
        else
            echo "    WARNING: $L not found in oven (IVFPQ bundle would skip it)"
        fi
    done

    echo
    "$PREFIX/bin/rampart" -c 'console.log("==> install OK. buildPlatform: " + rampart.buildPlatform)' || true
    ;;

  *)
    echo "unknown stage: $STAGE  (expected: build | test | install)" >&2
    exit 1
    ;;
esac
