# Minimal CMake wrapper for the vendored upa-url library.
# Bypasses upstream's conditional library gating
# (CMakeLists.upstream.txt only creates the library target if
# tests/install/etc. are enabled).
# We just need a plain static lib.

add_library(upa_url STATIC
    src/idna.cpp
    src/public_suffix_list.cpp
    src/unicode_id.cpp
    src/url.cpp
    src/url_ip.cpp
    src/url_search_params.cpp
    src/url_utf.cpp
    src/urlpattern.cpp
)

target_include_directories(upa_url PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(upa_url PUBLIC cxx_std_17)

# GCC 8 (e.g., Raspbian/Debian 10 on 32-bit Pi) needs explicit
# linkage of stdc++fs for std::filesystem usage inside upa.
target_link_libraries(upa_url PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)
