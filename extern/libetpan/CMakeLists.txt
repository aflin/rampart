# libetpan MIME/IMF parser -- a VENDORED SUBSET, not the upstream project.
# See README.rampart for what was taken, what was left behind, and why.

project(etpan_mime C)
cmake_minimum_required(VERSION 3.10...4.2)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(etpan_mime STATIC
    src/mailimf.c                 # RFC 5322 header parser
    src/mailimf_types.c
    src/mailmime.c                # MIME structure
    src/mailmime_content.c
    src/mailmime_decode.c         # RFC 2047 encoded-words
    src/mailmime_disposition.c
    src/mailmime_rfc2231.c        # RFC 2231 parameter continuations
    src/mailmime_types.c
    src/mailmime_types_helper.c
    src/chash.c                   # containers the parsers need
    src/clist.c
    src/mmapstring.c
    rp_charconv.c                 # ours: charconv() -> rp_charset_to_utf8()
)

# <libetpan/x.h> for the public form, and the same directory unprefixed
# because upstream's .c files include their own headers as "x.h".
target_include_directories(etpan_mime
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/libetpan
)

# rp_charconv.c calls into rampart's charset layer
target_include_directories(etpan_mime PRIVATE
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/duktape/core
)

# Upstream guards its POSIX includes -- and its LOCKING -- behind autoconf
# HAVE_* defines.  HAVE_CONFIG_H stays undefined so the vendored
# libetpan-config.h is used as-is, which means these have to be supplied
# here or the guarded code silently disappears.  Two ways that bit:
#
#   HAVE_UNISTD_H     mailmime_types_helper.c calls gethostname() and
#                     getpid() with <unistd.h> behind this guard.  Debian's
#                     headers pull it in transitively so a local build is
#                     fine; the manylinux 2_28 oven's glibc 2.28 headers do
#                     not, and the build fails on implicit declarations.
#
#   HAVE_PTHREAD_H    with LIBETPAN_REENTRANT, this is what makes
#   LIBETPAN_REENTRANT  MUTEX_LOCK in mmapstring.c an actual mutex instead of
#                     nothing at all.  That lock guards a PROCESS-GLOBAL
#                     reference-count hashtable which mmap_string_unref()
#                     touches for every decoded MIME part -- so without it,
#                     two server threads converting mail race on a shared
#                     hash table.  The mutex is PTHREAD_MUTEX_INITIALIZER,
#                     so no init call is needed.
#
# All five headers exist on every platform rampart targets (linux, macOS,
# FreeBSD); Windows is not a supported target.
target_compile_definitions(etpan_mime PRIVATE
    _DEFAULT_SOURCE
    HAVE_UNISTD_H
    HAVE_INTTYPES_H
    HAVE_SYS_TYPES_H
    HAVE_SYS_MMAN_H
    HAVE_PTHREAD_H
    LIBETPAN_REENTRANT
)

find_package(Threads REQUIRED)
target_link_libraries(etpan_mime PUBLIC Threads::Threads)
