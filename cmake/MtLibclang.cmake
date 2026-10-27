# SPDX-License-Identifier: Apache-2.0
# Part of mirrortest -- bidirectional C++ test/code generator.
#
# libclang, for the optional frontend that reads headers with a real compiler front end
# (docs/design.md, section 6.7; option MT_WITH_LIBCLANG, off by default).
#
# It is not fetched: libclang is the compiler's own library and has to match the compiler
# that finds the standard headers, so the system's is used. On Debian and Ubuntu that is
# `apt install libclang-dev`. When it is somewhere else, say where:
#
#   -DMT_LIBCLANG_INCLUDE_DIR=/opt/llvm/include   (the directory that holds clang-c/Index.h)
#   -DMT_LIBCLANG_LIBRARY=/opt/llvm/lib/libclang.so
#
# Licence: Apache-2.0 with LLVM exceptions, compatible with this project's.

set(MT_LIBCLANG_VERSIONS 22 21 20 19 18 17 16 15 14)

set(mt_llvm_hints "")
foreach(v IN LISTS MT_LIBCLANG_VERSIONS)
    list(APPEND mt_llvm_hints "/usr/lib/llvm-${v}" "/usr/lib64/llvm${v}" "/usr/lib/llvm${v}" "/opt/homebrew/opt/llvm@${v}" "/usr/local/opt/llvm@${v}")
endforeach()
list(APPEND mt_llvm_hints "/usr/lib/llvm" "/opt/homebrew/opt/llvm" "/usr/local/opt/llvm")

find_path(MT_LIBCLANG_INCLUDE_DIR clang-c/Index.h
    HINTS ${mt_llvm_hints}
    PATH_SUFFIXES include)
find_library(MT_LIBCLANG_LIBRARY
    NAMES clang libclang
    HINTS ${mt_llvm_hints}
    PATH_SUFFIXES lib lib64)

if(NOT MT_LIBCLANG_INCLUDE_DIR OR NOT MT_LIBCLANG_LIBRARY)
    message(FATAL_ERROR
        "MT_WITH_LIBCLANG is ON but libclang was not found.\n"
        "  header : ${MT_LIBCLANG_INCLUDE_DIR}\n"
        "  library: ${MT_LIBCLANG_LIBRARY}\n"
        "Install it (Debian/Ubuntu: libclang-dev), or point MT_LIBCLANG_INCLUDE_DIR at the directory "
        "that holds clang-c/Index.h and MT_LIBCLANG_LIBRARY at libclang.")
endif()

add_library(mt_libclang INTERFACE)
target_include_directories(mt_libclang SYSTEM INTERFACE "${MT_LIBCLANG_INCLUDE_DIR}")
target_link_libraries(mt_libclang INTERFACE "${MT_LIBCLANG_LIBRARY}")
add_library(mt::libclang ALIAS mt_libclang)
message(STATUS "mirrortest: libclang ${MT_LIBCLANG_LIBRARY} (headers in ${MT_LIBCLANG_INCLUDE_DIR})")
