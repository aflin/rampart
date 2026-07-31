#!/bin/sh
# build-installer.sh -- build the single-file rampart-install bundle.
#
# Lays out a minimal install image (rampart binary + a curated module set)
# under a stage directory, zips it, and concatenates the zip onto the bare
# rampart binary so the result is both a runnable rampart and a parseable
# zip.  At runtime the bundle's entry_script.js drives the install.
#
# Source priority for the binary and .so files:
#     1) /usr/local/rampart (a previous full install) -- already stripped
#     2) ./build/src        (this checkout's build)   -- stripped in-place
#                                                        in the stage dir
#
# The JS modules are always sourced from ./js_modules (or the equivalent
# in an existing install).
#
# Output: ./rampart-install  (next to this script)

set -e

# ----- module list ------------------------------------------------------
# Modify these three lists to change what ships in the lean install.

MODULES_SO="
    rampart-cmark.so
    rampart-crypto.so
    rampart-curl.so
    rampart-html.so
    rampart-lmdb.so
    rampart-net.so
    rampart-server.so
    rampart-sql.so
    rampart-totext.so
"

MODULES_JS="
    rampart-sqlUpdate.js
    rampart-webserver.js
    babel.js
    babel-polyfill.js
"

# Auxiliary binaries shipped alongside rampart in <prefix>/bin/.  rampart
# itself is NOT listed here -- it is sliced out of the bundle via
# payloadOffset() and written separately.
BIN_EXTRAS="
    addtable
    backref
    kdbfchk
    metamorph
    rex
    texislockd
    tsql
"

# System libraries shipped alongside rampart in <prefix>/lib/ so the
# rampart-sql.so + texislockd IVFPQ code path works on a vanilla install
# without a system package install of openblas+fortran runtime libs.
# rampart-sql.so and texislockd are linked with -Wl,-rpath,'$ORIGIN/../lib'
# so the dynamic loader finds these copies first.  Resolved by name
# against the system's default lib dirs at bundle-build time.
#
# Platform notes:
#   Linux   - libgomp is GNU's OpenMP runtime; ships with gcc.
#   FreeBSD - libomp is LLVM's, in the base toolchain (/usr/lib);
#             no need to bundle.  openblas + gfortran + quadmath are
#             port-installed and need to ride along.
# libquadmath is x86-specific (and ia64/sparc historically) -- Debian
# only ships libquadmath0 for those arches and aarch64's libgfortran
# doesn't DT_NEEDED it.  Include it only when the host arch will use it.
case "$(uname -m)" in
    x86_64|amd64|i?86) HAS_QUADMATH=1 ;;
    *)                 HAS_QUADMATH=0 ;;
esac

case "$(uname -s)" in
    Linux)
        SYSTEM_LIBS="
            libopenblas.so.0
            libgfortran.so.5
            libgomp.so.1
        "
        [ "$HAS_QUADMATH" = 1 ] && SYSTEM_LIBS="$SYSTEM_LIBS libquadmath.so.0"
        ;;
    FreeBSD)
        SYSTEM_LIBS="
            libopenblas.so.0
            libgfortran.so.5
        "
        [ "$HAS_QUADMATH" = 1 ] && SYSTEM_LIBS="$SYSTEM_LIBS libquadmath.so.0"
        ;;
    *)
        SYSTEM_LIBS=""
        ;;
esac

# ----- paths ------------------------------------------------------------

REPO=$(cd "$(dirname "$0")" && pwd)
# INSTALLED may be overridden in the environment (e.g. INSTALLED=/no-such
# to force the build/src fallback while iterating on the runtime).
INSTALLED=${INSTALLED:-/usr/local/rampart}
OUT=$REPO/rampart-install
DRIVER=$REPO/install/entry_script.js
UNINST=$REPO/install/uninstall.js
# INSTPK can be overridden by env (mkrp uses this to inject a
# channel-substituted copy of rampart-install.js before bundling).
INSTPK=${INSTPK:-$REPO/install/rampart-install.js}
PKGMAN=$REPO/install/packages.js

# The rampart-install.js source ships with an @@FROM_URL@@ placeholder
# that names the channel-specific package download URL.  mkrp resolves
# it (per channel: production -> downloads/rampart-<ver>, testing ->
# downloads/testing) and points $INSTPK at the substituted copy before
# invoking us.  If we still see the placeholder here, refuse to build a
# bundle that would later fail at `rampart --install pkg` with a bogus
# fetch URL.
if grep -q '@@FROM_URL@@' "$INSTPK" 2>/dev/null; then
    echo "build-installer: $INSTPK still contains @@FROM_URL@@ placeholder." >&2
    echo "                 The produced bundle would ship with an unresolved" >&2
    echo "                 package URL.  Run via mkrp (which substitutes per" >&2
    echo "                 channel), or pre-substitute and pass INSTPK env, e.g.:" >&2
    echo "" >&2
    echo "    sed 's|@@FROM_URL@@|<your-url>|g' install/rampart-install.js > /tmp/inst.js" >&2
    echo "    INSTPK=/tmp/inst.js ./build-installer.sh" >&2
    exit 1
fi

# Pick source for the rampart binary, .so files, JS modules, and the
# texis-derived extra binaries (addtable / metamorph / tsql / ...).
#
# Two source modes:
#   - "installed":  this script lives at <prefix>/build-installer.sh and
#                   pulls everything from <prefix>/{bin,modules}.  This is
#                   the normal `make install && cd /usr/local/rampart &&
#                   ./build-installer.sh` flow.
#   - "build":      this script lives at the checkout root and pulls
#                   from ./build/src, ./build/extern/texis/apps, and
#                   ./js_modules, stripping each artifact during staging.
if [ -x "$INSTALLED/bin/rampart" ] && [ -d "$INSTALLED/modules" ]; then
    SRC_BIN=$INSTALLED/bin/rampart
    SRC_SO_DIR=$INSTALLED/modules
    SRC_JS_DIR=$INSTALLED/modules        # JS modules ship alongside .so
    SRC_BIN_DIRS="$INSTALLED/bin"
    SRC_LICENSES_DIR=$INSTALLED/licenses
    NEED_STRIP=0
    echo "Using installed rampart from $INSTALLED"
elif [ -x "$REPO/build/src/rampart" ]; then
    SRC_BIN=$REPO/build/src/rampart
    SRC_SO_DIR=$REPO/build/src
    SRC_JS_DIR=$REPO/js_modules          # text source-of-truth in checkout
    # The texis apps live under build/extern/texis/apps; texislockd is in
    # build/src.  Try both, in order.
    SRC_BIN_DIRS="$REPO/build/src $REPO/build/extern/texis/apps"
    # In build mode licenses haven't been assembled in the checkout --
    # they only land under <prefix>/licenses after `make install`.  Fall
    # back to the installed copy if it exists; we still require the
    # directory below.
    SRC_LICENSES_DIR=$INSTALLED/licenses
    NEED_STRIP=1
    echo "Using build artifacts from $REPO/build/src (will strip)"
else
    echo "build-installer: cannot find rampart at $INSTALLED/bin/rampart or $REPO/build/src/rampart" >&2
    exit 1
fi

# Locate one of the BIN_EXTRAS in the first SRC_BIN_DIRS entry that has
# it; echoes the full path or empty.
find_bin() {
    _name=$1
    for _d in $SRC_BIN_DIRS; do
        if [ -f "$_d/$_name" ]; then
            echo "$_d/$_name"
            return 0
        fi
    done
    return 1
}

if [ ! -f "$DRIVER" ]; then
    echo "build-installer: installer driver not found at $DRIVER" >&2
    exit 1
fi
if [ ! -f "$UNINST" ]; then
    echo "build-installer: uninstall driver not found at $UNINST" >&2
    exit 1
fi
if [ ! -f "$INSTPK" ]; then
    echo "build-installer: rampart-install driver not found at $INSTPK" >&2
    exit 1
fi
if [ ! -f "$PKGMAN" ]; then
    echo "build-installer: packages manifest not found at $PKGMAN" >&2
    exit 1
fi
if [ ! -d "$SRC_LICENSES_DIR" ] || \
   [ -z "$(ls -A "$SRC_LICENSES_DIR" 2>/dev/null)" ]; then
    echo "build-installer: licenses dir empty or missing at $SRC_LICENSES_DIR" >&2
    exit 1
fi

# ----- pre-flight: every listed module must exist ----------------------

missing=
for m in $MODULES_SO; do
    [ -f "$SRC_SO_DIR/$m" ] || missing="$missing\n  $SRC_SO_DIR/$m"
done
for m in $MODULES_JS; do
    [ -f "$SRC_JS_DIR/$m" ] || missing="$missing\n  $SRC_JS_DIR/$m"
done
for b in $BIN_EXTRAS; do
    find_bin "$b" >/dev/null || missing="$missing\n  $b (searched: $SRC_BIN_DIRS)"
done
if [ -n "$missing" ]; then
    printf "build-installer: missing inputs:%b\n" "$missing" >&2
    exit 1
fi

# ----- stage layout ----------------------------------------------------
#
# The bundle layout mirrors the install layout so the installer can just
# payloadExtract(["bin/", "modules/"], "<prefix>") and have everything
# land where it belongs.  The bare rampart binary is sliced out
# separately via payloadOffset() and written to <prefix>/bin/rampart.
#
#   <stage>/entry_script.js        installer driver (not extracted)
#   <stage>/bin/<extras>           end up at <prefix>/bin/<...>
#   <stage>/modules/<*.so,*.js>    end up at <prefix>/modules/<...>

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/rampart-installer.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/bin" "$STAGE/modules" "$STAGE/lib"

cp "$DRIVER" "$STAGE/entry_script.js"
cp "$UNINST" "$STAGE/uninstall.js"
cp "$INSTPK" "$STAGE/rampart-install.js"
cp "$PKGMAN" "$STAGE/packages.js"

# Optional release notes -- baked into the bundle so entry_script.js can
# print them when the install finishes.  Not in git; a per-machine file.
#
# Preferred location is /usr/local/rampart-release-notes.txt, i.e.
# OUTSIDE the install prefix, sitting next to the /usr/local/rampart-build
# platform marker.  Two reasons:
#   - wiping and rebuilding <prefix> (or `rm -rf`ing it to start clean)
#     doesn't take the notes with it;
#   - one file serves every prefix on the machine, so the tiered Linux
#     hosts get the same notes into both the rampart-2_17 and the
#     rampart-2_28 bundles without keeping two copies in sync.
# The old in-prefix path still works as a fallback.
#
# A tiered Linux host builds TWO platforms from one machine, and they
# differ in what they ship (the 2_17 tier has no rampart-onnx and only
# CUDA 11; 2_28 has onnx and cu11/12/13).  So the per-platform name is
# checked first, falling back to the machine-wide one where the
# platforms have nothing different to say:
#
#   /usr/local/rampart-release-notes-<buildPlatform>.txt   e.g.
#   /usr/local/rampart-release-notes-linux-2_28-x86_64.txt
#   /usr/local/rampart-release-notes.txt
#   $INSTALLED/release-notes.txt                           (legacy)
HAVE_RELEASE_NOTES=0
RELEASE_NOTES_SRC=""
# buildPlatform is baked into the binary; take the part before the ';'
RN_OS=$("$INSTALLED/bin/rampart" -c 'console.log(rampart.buildPlatform)' 2>/dev/null | sed 's/;.*//' | tr -d '[:space:]')
if [ -n "$RN_OS" ] && [ -f "/usr/local/rampart-release-notes-${RN_OS}.txt" ]; then
    RELEASE_NOTES_SRC="/usr/local/rampart-release-notes-${RN_OS}.txt"
elif [ -f /usr/local/rampart-release-notes.txt ]; then
    RELEASE_NOTES_SRC=/usr/local/rampart-release-notes.txt
elif [ -f "$INSTALLED/release-notes.txt" ]; then
    RELEASE_NOTES_SRC="$INSTALLED/release-notes.txt"
fi
if [ -n "$RELEASE_NOTES_SRC" ]; then
    cp "$RELEASE_NOTES_SRC" "$STAGE/release-notes.txt"
    HAVE_RELEASE_NOTES=1
    echo "build-installer: release notes from $RELEASE_NOTES_SRC"
fi

# Bundle the assembled per-component licenses so the install lands them
# at <prefix>/licenses/.  cp -a preserves a flat regular-file directory.
mkdir -p "$STAGE/licenses"
cp -a "$SRC_LICENSES_DIR"/. "$STAGE/licenses/"

for b in $BIN_EXTRAS; do
    cp "$(find_bin "$b")" "$STAGE/bin/$b"
done
for m in $MODULES_SO; do
    cp "$SRC_SO_DIR/$m" "$STAGE/modules/$m"
done
for m in $MODULES_JS; do
    cp "$SRC_JS_DIR/$m" "$STAGE/modules/$m"
done

# System libs (libopenblas etc.) for rampart-sql.so + texislockd IVFPQ.
# Resolved via a portable search across the host's well-known library
# locations.  We copy the actual files (not symlinks) into the bundle
# so it stays self-contained; rampart-sql.so + texislockd still load
# them by SONAME at runtime via their $ORIGIN/../lib RPATH.
resolve_syslib() {
    _name=$1
    # 0) Prefer libs pre-staged into the install tree's lib/.  The docker oven's
    #    install stage drops the glibc<=2.17 BLAS chain there so a host-side
    #    bundle ships THOSE, not the host's newer (e.g. glibc 2.34) system copies.
    if [ -n "${INSTALLED:-}" ] && [ -e "$INSTALLED/lib/$_name" ]; then
        echo "$INSTALLED/lib/$_name"; return 0
    fi
    # 1) Directory walk -- ordered so port/multiarch dirs come first
    #    (Linux multiarch + FreeBSD ports), then the more generic /lib
    #    and /usr/lib.  Includes gcc14/13/12 subdirs where FreeBSD's
    #    gcc port keeps libgfortran / libquadmath.
    for _d in /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu \
              /lib/x86_64-linux-gnu /lib/aarch64-linux-gnu \
              /usr/local/lib \
              /usr/local/lib/gcc14 /usr/local/lib/gcc13 /usr/local/lib/gcc12 \
              /lib /usr/lib; do
        if [ -e "$_d/$_name" ]; then echo "$_d/$_name"; return 0; fi
    done
    # 2) Linux ldconfig cache fallback
    if [ "$(uname -s)" = "Linux" ] && [ -x /sbin/ldconfig ]; then
        _p=$(/sbin/ldconfig -p 2>/dev/null | awk -v lib="$_name" \
                '$1==lib { print $NF; exit }')
        [ -n "$_p" ] && [ -e "$_p" ] && { echo "$_p"; return 0; }
    fi
    return 1
}

SYSLIB_MISSING=
for L in $SYSTEM_LIBS; do
    src=$(resolve_syslib "$L")
    if [ -z "$src" ] || [ ! -e "$src" ]; then
        SYSLIB_MISSING="$SYSLIB_MISSING $L"
        continue
    fi
    # Resolve through the symlink chain so the bundled file is the real one
    # (the loader still loads $L by SONAME via DT_NEEDED).
    realsrc=$(readlink -f "$src")
    cp "$realsrc" "$STAGE/lib/$L"
done
if [ -n "$SYSLIB_MISSING" ]; then
    case "$(uname -s)" in
        Linux)   _hint="apt install needed" ;;
        FreeBSD) _hint="pkg install needed (openblas, gcc14)" ;;
        *)       _hint="install needed" ;;
    esac
    echo "build-installer: missing system libs (${_hint}):${SYSLIB_MISSING}" >&2
    exit 1
fi

# Patch each bundled lib's own RPATH to "$ORIGIN" so it finds its
# SIBLING libs in the same directory.  Without this, e.g. FreeBSD's
# port-built libgfortran.so.5 has an RPATH baked in pointing at
# /usr/local/lib/gcc14 (gone on a vanilla install), and its DT_NEEDED
# libquadmath.so.0 can't be resolved -- even though we ship
# libquadmath next to libgfortran in <prefix>/lib/.
#
# patchelf is a Linux/FreeBSD ELF tool; on macOS no libs are bundled
# (SYSTEM_LIBS is empty since FAISS uses Accelerate), so skip the
# whole block when there's nothing to patch.
if [ -n "$SYSTEM_LIBS" ]; then
    if ! command -v patchelf >/dev/null 2>&1; then
        echo "build-installer: 'patchelf' not found; bundled libs may have stale RPATHs" >&2
        echo "  install with: pkg install patchelf  (FreeBSD)  /  apt install patchelf  (Debian)" >&2
        exit 1
    fi
    for L in $SYSTEM_LIBS; do
        [ -f "$STAGE/lib/$L" ] || continue
        patchelf --set-rpath '$ORIGIN' "$STAGE/lib/$L" || \
            { echo "patchelf failed on $L" >&2; exit 1; }
    done
fi

# Stage the bare binary too (it'll get sliced out by the installer using
# payloadOffset(); we still need it as the SFX stub).
cp "$SRC_BIN" "$STAGE/rampart-stub"

if [ "$NEED_STRIP" -eq 1 ]; then
    # macOS's strip(1) is too aggressive for .so/.dylib loaded via dlopen
    # -- it tries to remove globals referenced by the indirect symbol
    # table and fails with "symbols referenced by indirect symbol table
    # entries that can't be stripped".  Use -x -S there; on Linux/FreeBSD
    # plain `strip` is fine.
    case "$(uname -s)" in
        Darwin) STRIP_ARGS="-x -S" ;;
        *)      STRIP_ARGS=""      ;;
    esac
    strip $STRIP_ARGS "$STAGE/rampart-stub"
    for b in $BIN_EXTRAS; do
        strip $STRIP_ARGS "$STAGE/bin/$b"
    done
    for m in $MODULES_SO; do
        strip $STRIP_ARGS "$STAGE/modules/$m"
    done
fi

# ----- build the zip and concatenate -----------------------------------

RELEASE_NOTES_ARG=""
[ "$HAVE_RELEASE_NOTES" = "1" ] && RELEASE_NOTES_ARG="release-notes.txt"
( cd "$STAGE" && zip -qr payload.zip entry_script.js uninstall.js rampart-install.js packages.js bin modules lib licenses $RELEASE_NOTES_ARG )

cp "$STAGE/rampart-stub" "$OUT"
cat "$STAGE/payload.zip" >> "$OUT"
chmod 0755 "$OUT"

# Size report (BSD vs GNU stat).
size=$(stat -c '%s' "$OUT" 2>/dev/null || stat -f '%z' "$OUT")
bare=$(stat -c '%s' "$STAGE/rampart-stub" 2>/dev/null || stat -f '%z' "$STAGE/rampart-stub")
zipsz=$(stat -c '%s' "$STAGE/payload.zip" 2>/dev/null || stat -f '%z' "$STAGE/payload.zip")
echo "Built: $OUT"
echo "       total  ${size} bytes  (bare rampart ${bare} + payload ${zipsz})"
