# SPDX-License-Identifier: Apache-2.0
# Part of mirrortest -- bidirectional C++ test/code generator.
#
# Do the stubs of a gen-impl golden case compile, under the strictest rules
# a project is likely to hold them to? Run as
#
#   cmake -DMT_BIN=... -DMT_CASE_DIR=... -DMT_WORK=... -DMT_CXX=... -P syntax_case.cmake
#
# It generates from the case's test (its args.txt, as the golden runner reads
# it) and compiles every generated source with `-fsyntax-only` as C++11 with
# -Wall -Wextra -Wpedantic -Werror, plus -fno-exceptions -fno-rtti when the case
# asks for --no-exceptions. Nothing is linked or run, so MT_CXX can be a cross
# compiler for a 32-bit target: CI uses g++-arm-linux-gnueabihf.
#
# Each source includes its own header first, so a header that is not
# self-contained fails here too.
#
# Stubs that mention Qt (a QString in a signature) need Qt's headers, which are
# C++17: with -DMT_QT_INCLUDE=<list> the stubs are compiled as C++17 with -fPIC
# and Qt's include directories, and nothing else changes.

cmake_minimum_required(VERSION 3.20)

foreach(var MT_BIN MT_CASE_DIR MT_WORK MT_CXX)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "syntax_case.cmake: -D${var}=... is required")
    endif()
endforeach()

set(input "${MT_CASE_DIR}/input")
set(gen "${MT_WORK}/gen")
file(REMOVE_RECURSE "${MT_WORK}")
file(MAKE_DIRECTORY "${gen}")

file(STRINGS "${MT_CASE_DIR}/args.txt" raw_args)
set(args "")
set(flags -std=c++11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only)
if(DEFINED MT_QT_INCLUDE AND NOT "${MT_QT_INCLUDE}" STREQUAL "")
    set(flags -std=c++17 -fPIC -Wall -Wextra -Wpedantic -Werror -fsyntax-only)
    foreach(dir IN LISTS MT_QT_INCLUDE)
        list(APPEND flags "-I${dir}")
    endforeach()
endif()
foreach(arg IN LISTS raw_args)
    string(REPLACE "\${IN}" "${input}" arg "${arg}")
    string(REPLACE "\${OUT}" "${gen}" arg "${arg}")
    list(APPEND args "${arg}")
    if(arg STREQUAL "--no-exceptions")
        list(APPEND flags -fno-exceptions -fno-rtti)
    endif()
endforeach()

execute_process(COMMAND "${MT_BIN}" ${args} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "gen-impl exited ${code} for ${MT_CASE_DIR}:\n${out}${err}")
endif()

file(GLOB_RECURSE sources "${gen}/*.cpp")
list(SORT sources)
foreach(source IN LISTS sources)
    execute_process(COMMAND "${MT_CXX}" ${flags} "-I${gen}" "-I${input}/include" "${source}"
                    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "${MT_CXX} rejects ${source} (${MT_CASE_DIR}):\n${out}${err}")
    endif()
endforeach()
list(LENGTH sources count)
message(STATUS "${count} source file(s) compile with ${MT_CXX}")
