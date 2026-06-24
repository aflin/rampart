#!/bin/sh
# build.sh <stage> --     build a centos7-x86_64 (glibc 2.17)  or 
#                         a almalinux 8 (glibc 2.28) rampart in a
#                         manylinux "oven", in three distinct stages.
#
#   build.sh build             # a) grab libraries + compile -> build/manylinux2014/
#   build.sh test              # b) run build/manylinux2014/src/run_tests.sh
#   build.sh install           # c) replace /usr/local/rampart-ml (asks first)
#   build.sh all               #    build + test + install
#   build.sh shell             #    interactive shell in the oven
#   build.sh save-image        #    persist the oven image to a .tar.gz
#
#   Flags:
#      --rebuild-image [stage] # force a fresh oven image first
#      --yes                   # skip build.sh install confirmation
#      -b 2014|2_28            # build base: 2014=glibc 2.17 (default) or 
#                              # 2_28=glibc 2.28 (gcc-toolset / GPU base) 
#      -d <dir>                # install into <dir> instead of /usr/local/rampart-ml        
#
# What it touches:
#   build      -> build/manylinux[2014|2_28]/
#   test       -> nothing (reads the build dir)
#   install    -> REPLACES /usr/local/rampart-ml  (or -d specified directory)
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
# Leading options (any order): -d <install-dir>, -b <2014|2_28> (base/glibc tier).
PREFIX_DIR=""; BASE=2014
while :; do case "${1:-}" in
    -d) PREFIX_DIR="$2"; shift 2 ;;
    -b) BASE="$2"; shift 2 ;;
    *)  break ;;
esac; done
case "$BASE" in
    2014) IMAGE=rampart-manylinux2014;  DOCKERFILE=Dockerfile;      BUILDSUBDIR=manylinux2014 ;;
    2_28) IMAGE=rampart-manylinux_2_28; DOCKERFILE=Dockerfile.2_28; BUILDSUBDIR=manylinux_2_28 ;;
    *)    echo "unknown base: $BASE (expected 2014 | 2_28)" >&2; exit 1 ;;
esac
[ -n "$PREFIX_DIR" ] || PREFIX_DIR="/usr/local/rampart-$( [ "$BASE" = 2_28 ] && echo 2_28 || echo 2_17 )"
IMAGE_TAR="$REPO/build/$IMAGE.image.tar.gz"   # persisted image (gitignored build/)

# Persist the built image to a .tar.gz so it can be restored with `docker load`
# instead of a full rebuild (OpenBLAS ~15 min) if the image or buildkit cache is
# later pruned, or moved to another machine.  Invoked only by the `save-image`
# stage -- builds no longer create the tarball automatically (it is large).
save_image() {
    mkdir -p "$(dirname "$IMAGE_TAR")"
    echo "==> persisting image to $IMAGE_TAR"
    docker save "$IMAGE" | gzip > "$IMAGE_TAR"
}

if [ "$1" = "--rebuild-image" ]; then
    docker build --no-cache --build-arg ARCH="$(uname -m)" -t "$IMAGE" -f "$HERE/$DOCKERFILE" "$HERE"
    shift
fi

# Ensure the oven image exists (the "grab libraries" step: yum deps + OpenBLAS
# from source; cached after the first build).  Only stages that need the
# container call this, so --help/usage never triggers a build.
ensure_image() {
    # NEVER rebuild OpenBLAS for an ordinary rampart change.  Precedence:
    #   1) image already loaded -> use it
    #   2) restore from the persisted tarball -> `docker load` (no rebuild)
    #   3) only if neither exists -> build from scratch (run `save-image` to persist)
    # OpenBLAS lives in the image, so once the image exists (or is restored),
    # `docker/build.sh build` just does an incremental compile in
    # build/manylinux2014 -- exactly like building outside the container.
    # After editing the Dockerfile, rebuild explicitly: build.sh --rebuild-image
    if docker image inspect "$IMAGE" >/dev/null 2>&1; then
        echo "==> using existing oven image '$IMAGE' (run --rebuild-image after Dockerfile edits)"
        return
    fi
    if [ -f "$IMAGE_TAR" ]; then
        echo "==> restoring oven image from $IMAGE_TAR (no rebuild)"
        docker load -i "$IMAGE_TAR" && return
        echo "   (load failed -- rebuilding)"
    fi
    echo "==> building oven image '$IMAGE' (one-time: yum deps + OpenBLAS)…"
    docker build --build-arg ARCH="$(uname -m)" -t "$IMAGE" -f "$HERE/$DOCKERFILE" "$HERE"
}

# ---- stage runners ---------------------------------------------------------
do_build() {
    ensure_image
    echo "==> [build] compiling rampart into build/$BUILDSUBDIR/ (install prefix: $PREFIX_DIR)…"
    # Pass the install prefix at BUILD time: rampart's CMakeLists bakes the
    # rampart binary + modules install paths as ABSOLUTE (from CMAKE_INSTALL_PREFIX)
    # at configure -- `cmake --install --prefix` can't move those later.  So the
    # install dir is decided here; `install` must use the same -b/-d.
    docker run --rm \
        -e HOME=/tmp -e RAMPART_BUILDDIR="$BUILDSUBDIR" -e RAMPART_PREFIX="$PREFIX_DIR" \
        -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
        -v "$REPO:/src" -w /src \
        "$IMAGE" /src/docker/build-in-oven.sh build
}

do_test() {
    ensure_image
    echo "==> [test] running run_tests.sh in build/manylinux2014/src…"
    # Run as the invoking user (not root) so run_tests.sh skips its root prompt.
    # Mount /etc/passwd ro so the uid resolves to a name (whoami works).
    docker run --rm \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp -e RAMPART_BUILDDIR="$BUILDSUBDIR" \
        -v /etc/passwd:/etc/passwd:ro -v /etc/group:/etc/group:ro \
        -v "$REPO:/src" -w /src \
        "$IMAGE" /src/docker/build-in-oven.sh test
}

do_install() {
    ensure_image
    if [ "${ASSUME_YES:-0}" -ne 1 ]; then
        printf "Install/update rampart in %s? (overwrites rampart's own files; other modules kept) [y/N] " "$PREFIX_DIR"
        read ans
        case "$ans" in [yY]*) ;; *) echo "Aborted."; exit 1 ;; esac
    fi
    echo "==> [install] installing into $PREFIX_DIR…"
    # root (default) to write $PREFIX_DIR; only that subdir is mounted (mounting
    # all of /usr/local would hide the oven's cmake + OpenBLAS).  Mounted at its
    # REAL path so cmake installs to -- and records -- $PREFIX_DIR, not a remap.
    docker run --rm \
        -e HOME=/tmp -e RAMPART_BUILDDIR="$BUILDSUBDIR" -e RAMPART_PREFIX="$PREFIX_DIR" \
        -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
        -v "$REPO:/src" -w /src \
        -v "$PREFIX_DIR":"$PREFIX_DIR" \
        "$IMAGE" /src/docker/build-in-oven.sh install
}

# ---- dispatch --------------------------------------------------------------
STAGE="${1:-}"
[ "${2:-}" = "--yes" ] && ASSUME_YES=1

case "$STAGE" in
    build)   do_build ;;
    test)    do_test ;;
    install) do_install ;;
    all)     do_build; echo; do_test; echo; do_install ;;
    save-image)
        docker image inspect "$IMAGE" >/dev/null 2>&1 || {
            echo "image '$IMAGE' not built yet -- run 'docker/build.sh build' first" >&2; exit 1; }
        save_image ;;
    shell)
        ensure_image
        exec docker run --rm -it -e HOME=/tmp \
            -v "$REPO:/src" -w /src "$IMAGE" /bin/bash ;;
    ""|-h|--help)
        sed -n '2,/^set -e/{/^set -e/!p}' "$0" | sed 's/^# \{0,1\}//' ;;
    *)
        echo "unknown stage: $STAGE  (build | test | install | all | save-image | shell)" >&2
        exit 1 ;;
esac
