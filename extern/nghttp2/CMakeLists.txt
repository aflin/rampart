# nghttp2 1.70.0 (MIT) -- libnghttp2 only, for HTTP/2 in the vendored curl.
# Not the upstream build: no apps, tests, examples or docs, and no
# dependency probing.  The upstream file is kept as CMakeLists.upstream.txt.
# lib/includes/nghttp2/nghttp2ver.h is pre-generated in the release tarball.
cmake_minimum_required(VERSION 3.10...4.2)
project(nghttp2 VERSION 1.70.0 LANGUAGES C)

set(NGHTTP2_SOURCES
  nghttp2_pq.c nghttp2_map.c nghttp2_queue.c
  nghttp2_frame.c
  nghttp2_buf.c
  nghttp2_stream.c nghttp2_outbound_item.c
  nghttp2_session.c nghttp2_submit.c
  nghttp2_helper.c
  nghttp2_alpn.c
  nghttp2_hd.c nghttp2_hd_huffman.c nghttp2_hd_huffman_data.c
  nghttp2_version.c
  nghttp2_priority_spec.c
  nghttp2_option.c
  nghttp2_callbacks.c
  nghttp2_mem.c
  nghttp2_http.c
  nghttp2_rcbuf.c
  nghttp2_extpri.c
  nghttp2_ratelim.c
  nghttp2_time.c
  nghttp2_debug.c
  sfparse.c
)
list(TRANSFORM NGHTTP2_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/lib/")

# static, PIC: linked into rampart-curl.so
add_library(nghttp2_rp STATIC ${NGHTTP2_SOURCES})
set_target_properties(nghttp2_rp PROPERTIES
  OUTPUT_NAME nghttp2
  POSITION_INDEPENDENT_CODE ON)
target_include_directories(nghttp2_rp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lib/includes")
# what upstream's config.h would give on every supported platform (all POSIX)
target_compile_definitions(nghttp2_rp
  PRIVATE BUILDING_NGHTTP2 HAVE_ARPA_INET_H HAVE_NETINET_IN_H
          HAVE_CLOCK_GETTIME HAVE_DECL_CLOCK_MONOTONIC=1
  PUBLIC  NGHTTP2_STATICLIB)
