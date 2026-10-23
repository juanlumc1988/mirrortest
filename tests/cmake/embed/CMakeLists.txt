# SPDX-License-Identifier: Apache-2.0
# Part of mirrortest -- bidirectional C++ test/code generator.
#
# Drives cmake/MtEmbedFiles.cmake in isolation. Run by CTest as a configure:
#   -DCASE=ok     the generated source must carry awkward templates intact
#   -DCASE=clash  the configure must fail, naming the raw-string terminator

cmake_minimum_required(VERSION 3.20)
project(mt_embed_check NONE)

include("${MT_SOURCE_DIR}/cmake/MtEmbedFiles.cmake")

if(CASE STREQUAL "clash")
    mt_embed_templates("${CMAKE_BINARY_DIR}/out.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/clash")
    message(FATAL_ERROR "the embedding accepted a template containing )mt_tpl\"")
endif()

mt_embed_templates("${CMAKE_BINARY_DIR}/out.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/ok")
file(READ "${CMAKE_BINARY_DIR}/out.cpp" generated)

function(expect text what)
    string(FIND "${generated}" "${text}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "generated source lacks ${what}:\n${generated}")
    endif()
    set(found_at ${pos} PARENT_SCOPE)
endfunction()

# Awkward characters survive: ';' '[' ']' '"' '\' and text that looks like a
# CMake or configure_file variable.
expect("semi;colon [brackets] \"quotes\" \\backslash \${NOT_EXPANDED} @AT@ {{ x }}\nsecond line\n"
       "the awkward template, verbatim")

# CRLF becomes LF.
expect("{\"sub/crlf.inja\", R\"mt_tpl(line one\nline two\n)mt_tpl\"}" "the CRLF template, as LF")
string(FIND "${generated}" "\r" carriage_return)
if(NOT carriage_return EQUAL -1)
    message(FATAL_ERROR "a carriage return reached the generated source")
endif()

# Sorted by relative path with '/' separators.
expect("{\"nasty.inja\"" "nasty.inja")
set(first ${found_at})
expect("{\"sub/crlf.inja\"" "sub/crlf.inja")
if(NOT first LESS found_at)
    message(FATAL_ERROR "entries are not sorted by name")
endif()

# A second run with nothing changed must not rewrite the file. Timestamps have
# a resolution of one second, so wait one out or the check proves nothing.
file(TIMESTAMP "${CMAKE_BINARY_DIR}/out.cpp" before "%s")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
mt_embed_templates("${CMAKE_BINARY_DIR}/out.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/ok")
file(TIMESTAMP "${CMAKE_BINARY_DIR}/out.cpp" after "%s")
if(NOT before STREQUAL after)
    message(FATAL_ERROR "an unchanged embedding rewrote its output")
endif()
message(STATUS "embedding checks passed")
