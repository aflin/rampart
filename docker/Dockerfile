# Rampart "build oven" — produce a portable Linux bundle with a low glibc floor.
#
# Base = manylinux2014 (CentOS 7 / glibc 2.17 + a modern gcc via devtoolset),
# the same environment the Python-wheel ecosystem uses to run "old -> new"
# across distros.  Binaries built here carry a GLIBC floor of ~2.17
# (RHEL7+, Ubuntu 14.04+, Debian 8+) instead of this host's 2.36.
#
# This image is a TOOL, not part of the normal build.  It is used ONLY by
# docker/build.sh.  Nothing in any CMakeLists.txt references it, so the
# ordinary `mkdir build && cd build && cmake .. && make -j` flow is untouched.
#
# Build it (one-time, cached):   docker build -t rampart-manylinux2014 docker/
# It is built automatically by docker/build.sh if missing.

# Target arch: x86_64 (default) or aarch64.  build.sh passes --build-arg ARCH=$(uname -m).
ARG ARCH=x86_64
FROM quay.io/pypa/manylinux2014_${ARCH}

# devtoolset's `enable` script is a bash-ism; make every RUN a bash shell.
SHELL ["/bin/bash", "-c"]

# --- build-time dependencies (CentOS 7 / EPEL package names) ----------------
# Mapped from the actual `apt install` set used on a fresh Debian 11 build
# (apt history), NOT the stale README.  Mapping notes:
#   build-essential, git, cmake, curl, perl  -> provided by the manylinux base
#   gfortran, OpenMP (libgomp)               -> provided by the devtoolset gcc
#   libopenblas-dev                          -> built from source below (ABI)
#   libgdbm-compat-dev                       -> covered by gdbm-devel on CentOS
#   patchelf                                 -> RPATH fixups (install + packaging)
#   GraphicsMagick-devel                      -> rampart-graphicsmagick.so
#                                                (provides GraphicsMagickWand-config)
# Intentionally EXCLUDED for now (per scope = "main rampart only"):
#   libwebkit2gtk/libgtk-3-dev (rampart-webview), nvidia-cuda-toolkit (cuda).
# NONE of these become runtime deps: the vendored Python statically embeds
# sqlite/gdbm/bz2/lzma/ffi/ncurses/readline/tcl/tk/uuid, and OpenSSL 4.0.0 is
# vendored + linked statically (verified: ssl.OPENSSL_VERSION == 4.0.0).
RUN yum install -y epel-release && \
    yum install -y \
        flex bison perl python3 patchelf \
        zlib-devel \
        libidn2-devel openldap-devel \
        sqlite-devel libuuid-devel \
        tcl-devel tk-devel \
        gdbm-devel bzip2-devel xz-devel libffi-devel \
        ncurses-devel readline-devel \
        GraphicsMagick-devel && \
    yum clean all

# --- OpenBLAS built from source against the devtoolset toolchain ------------
# Built here (rather than via EPEL) so its libgfortran dependency is the
# devtoolset libgfortran.so.5 (glibc-2.17 safe) that matches what FAISS in
# rampart-sql expects -- EPEL's openblas links the base gcc-4.8
# libgfortran.so.3, which would be an ABI mismatch.
#
# Pinned to 0.3.21 to match the version the host bundle currently ships.
# DYNAMIC_ARCH=1 => one binary with runtime CPU dispatch (portable bundle).
# Installs into /usr/local/lib, where build-installer.sh already searches for
# the SYSTEM_LIBS (libopenblas/libgfortran/libgomp/libquadmath) it bundles.
ARG OPENBLAS_VERSION=0.3.21
RUN source "$(ls /opt/rh/devtoolset-*/enable | sort -V | tail -1)" && \
    curl -fsSL -o /tmp/openblas.tar.gz \
      "https://github.com/OpenMathLib/OpenBLAS/releases/download/v${OPENBLAS_VERSION}/OpenBLAS-${OPENBLAS_VERSION}.tar.gz" && \
    tar -xzf /tmp/openblas.tar.gz -C /tmp && \
    make -C "/tmp/OpenBLAS-${OPENBLAS_VERSION}" -j"$(nproc)" \
        DYNAMIC_ARCH=1 NUM_THREADS=64 USE_OPENMP=1 && \
    make -C "/tmp/OpenBLAS-${OPENBLAS_VERSION}" PREFIX=/usr/local install && \
    rm -rf /tmp/OpenBLAS-* /tmp/openblas.tar.gz && \
    echo /usr/local/lib > /etc/ld.so.conf.d/openblas.conf && ldconfig

# --- static libidn2 built from source ---------------------------------------
# rampart's vendored curl is patched to link the STATIC archive
# (extern/curl/CMakeLists.txt: find_library(IDN2 libidn2.a)).  RHEL/CentOS
# strips static libs, so EPEL's libidn2-devel ships only libidn2.so -- no
# libidn2.a -- and the curl build NOTFOUNDs.  Debian's libidn2-dev includes
# the .a, which is why the host links it statically.  Build it ourselves with
# devtoolset (glibc-2.17 safe).  Built WITHOUT system libunistring so libidn2
# uses its bundled unistring fallback -> the resulting libidn2.a is
# self-contained (no libunistring runtime dep leaks into rampart-curl.so).
# Installs into /usr/local/lib, which find_library searches by default.
# Appended after OpenBLAS so editing it never invalidates the OpenBLAS layer.
ARG LIBIDN2_VERSION=2.3.7
RUN source "$(ls /opt/rh/devtoolset-*/enable | sort -V | tail -1)" && \
    curl -fsSL -o /tmp/libidn2.tar.gz \
      "https://ftp.gnu.org/gnu/libidn/libidn2-${LIBIDN2_VERSION}.tar.gz" && \
    tar -xzf /tmp/libidn2.tar.gz -C /tmp && \
    cd "/tmp/libidn2-${LIBIDN2_VERSION}" && \
    ./configure --prefix=/usr/local --enable-static --disable-shared \
        --disable-doc --disable-gtk-doc --with-included-libunistring && \
    make -j"$(nproc)" && make install && \
    rm -rf /tmp/libidn2-* /tmp/libidn2.tar.gz

# --- extra Perl modules for the vendored OpenSSL 4.0.0 Configure ------------
# CentOS 7's minimal `perl` package omits modules that OpenSSL's Configure
# requires (IPC::Cmd, Data::Dumper, ...).  Debian's perl bundles these, which
# is why the host build "just works".  Separate layer (appended last) so it
# never invalidates the cached OpenBLAS / libidn2 layers.
RUN yum install -y perl-core perl-IPC-Cmd && yum clean all

# --- newer GCC (devtoolset-11) for AVX-512 SimSIMD kernels ------------------
# manylinux2014's default devtoolset-10 (GCC 10) can't compile SimSIMD's
# AVX-512 path: rampart's CMake probe uses _mm512_loadu_epi8, added in GCC 11,
# so the build falls back to AVX2-only.  devtoolset-11 (GCC 11.2, from the
# centos-sclo-rh repo) lets the AVX-512 (Skylake/Ice/Genoa) kernels compile in;
# enable_toolchain picks the highest devtoolset, so the build auto-uses 11.
# Still CentOS 7 => glibc 2.17 floor unchanged, and devtoolset statically links
# newer libstdc++ symbols (dynamic GLIBCXX stays <= 3.4.19), so the binary
# still runs on vanilla CentOS 7.  Appended last to preserve earlier layers.
ARG ARCH=x86_64
RUN if [ "$ARCH" = "x86_64" ]; then \
        yum install -y devtoolset-11-gcc devtoolset-11-gcc-c++ \
            devtoolset-11-gcc-gfortran devtoolset-11-binutils && yum clean all; \
    else \
        echo "==> $ARCH: CentOS 7 has no devtoolset-11 (and no AVX-512 on arm); using the image's bundled devtoolset-10 (gcc/g++/gfortran 10.2)"; \
    fi

# (rampart-webview is NOT built here -- CentOS 7's webkit2gtk 2.28 / GLib 2.56
# are too old.  It has its own oven at rampart-webview/docker/ on a newer base.)

# No ENTRYPOINT: docker/build.sh invokes docker/build-in-oven.sh explicitly so
# the same image can be reused for ad-hoc shells (docker run ... bash).
