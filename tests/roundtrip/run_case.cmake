# SPDX-License-Identifier: Apache-2.0
# Part of mirrortest -- bidirectional C++ test/code generator.
#
# One gen-impl round trip (docs/design.md, section 16.3), run as
#
#   cmake -DMT_BIN=... -DMT_CASE_DIR=... -DMT_WORK=... -P run_case.cmake
#
# The question it answers is the one TDD asks of the tool: does what it
# generates make the test compile, and does the test then fail? Not "does it
# look right" (the golden files do that) but "does a compiler accept it and does
# running it say red".
#
#   1. gen-impl over the test of a golden case, into <work>/gen
#   2. the stubs compiled as the strictest embedded code would be: C++11,
#      -Wall -Wextra -Wpedantic -Werror (plus -fno-exceptions -fno-rtti for the
#      abort policy), with the headers the case brings (input/include) on -I
#   3. the test compiled as the project's tests are: C++14, -Wall -Wextra -Werror
#   4. run: every test must fail. With stubs that throw, GoogleTest reports the
#      failures, so `failures == tests` in its JSON. With stubs that abort the
#      process dies instead; each test is run alone and must die.
#   5. gen-impl again with --check: exit 0 means no file would change, which is
#      the idempotence promise
#
# Inputs (-D):
#   MT_BIN          the mirrortest executable
#   MT_CASE_DIR     tests/golden/gen-impl/<case>: its args.txt and input/ are used
#   MT_WORK         scratch directory, emptied first
#   MT_CXX          the C++ compiler
#   MT_CXX_FLAGS    flags shared with the main build (sanitizers, for one)
#   MT_GTEST_INCLUDE, MT_GTEST_LIBS   GoogleTest as the main build has it (lists)
#   MT_MODE         "throw" (default) or "abort"

cmake_minimum_required(VERSION 3.20)

foreach(var MT_BIN MT_CASE_DIR MT_WORK MT_CXX MT_GTEST_INCLUDE MT_GTEST_LIBS)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "run_case.cmake: -D${var}=... is required")
    endif()
endforeach()
if(NOT DEFINED MT_MODE)
    set(MT_MODE throw)
endif()

set(input "${MT_CASE_DIR}/input")
set(gen "${MT_WORK}/gen")
file(REMOVE_RECURSE "${MT_WORK}")
file(MAKE_DIRECTORY "${gen}")

function(fail message)
    message(FATAL_ERROR "round trip failed for ${MT_CASE_DIR}: ${message}")
endfunction()

# Runs a command; fails with its output unless it exits with `expected`.
function(run description expected)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT "${code}" STREQUAL "${expected}")
        fail("${description}: exit ${code}, expected ${expected}\n${out}${err}")
    endif()
    set(last_out "${out}" PARENT_SCOPE)
    set(last_err "${err}" PARENT_SCOPE)
endfunction()

# ---- 1. generate ----------------------------------------------------------------------------------------------
file(STRINGS "${MT_CASE_DIR}/args.txt" raw_args)
set(args "")
foreach(arg IN LISTS raw_args)
    string(REPLACE "\${IN}" "${input}" arg "${arg}")
    string(REPLACE "\${OUT}" "${gen}" arg "${arg}")
    list(APPEND args "${arg}")
    if(arg MATCHES "_test\\.cpp$")
        set(test_file "${arg}")
    endif()
endforeach()
if(NOT DEFINED test_file)
    fail("no *_test.cpp among the arguments of ${MT_CASE_DIR}/args.txt")
endif()

run("gen-impl" 0 "${MT_BIN}" ${args})

# ---- 2. the stubs ---------------------------------------------------------------------------------------------
separate_arguments(shared UNIX_COMMAND "${MT_CXX_FLAGS}")
set(stub_flags -std=c++11 -Wall -Wextra -Wpedantic -Werror)
if(MT_MODE STREQUAL "abort")
    list(APPEND stub_flags -fno-exceptions -fno-rtti)
endif()

file(GLOB_RECURSE stubs "${gen}/*.cpp")
list(SORT stubs)
if(NOT stubs)
    fail("gen-impl wrote no source file")
endif()
set(objects "")
set(index 0)
foreach(stub IN LISTS stubs)
    set(object "${MT_WORK}/stub${index}.o")
    run("compiling ${stub}" 0 "${MT_CXX}" ${shared} ${stub_flags} "-I${gen}" "-I${input}/include" -c "${stub}" -o "${object}")
    list(APPEND objects "${object}")
    math(EXPR index "${index} + 1")
endforeach()

# ---- 3. the test ----------------------------------------------------------------------------------------------
# MT_GTEST_INCLUDE is a list: GoogleTest's target exports more than one directory.
set(gtest_includes "")
foreach(dir IN LISTS MT_GTEST_INCLUDE)
    list(APPEND gtest_includes "-I${dir}")
endforeach()

set(test_object "${MT_WORK}/test.o")
run("compiling ${test_file}" 0 "${MT_CXX}" ${shared} -std=c++14 -Wall -Wextra -Werror
    "-I${gen}" "-I${input}" "-I${input}/include" ${gtest_includes} -c "${test_file}" -o "${test_object}")

set(binary "${MT_WORK}/roundtrip")
run("linking" 0 "${MT_CXX}" ${shared} "${test_object}" ${objects} ${MT_GTEST_LIBS} -pthread -o "${binary}")

# ---- 4. every test fails --------------------------------------------------------------------------------------
if(MT_MODE STREQUAL "throw")
    set(report "${MT_WORK}/report.json")
    execute_process(COMMAND "${binary}" "--gtest_output=json:${report}" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(code EQUAL 0)
        fail("the test binary passed, it must fail:\n${out}")
    endif()
    if(NOT EXISTS "${report}")
        fail("no JSON report (did the binary crash?):\n${out}${err}")
    endif()
    file(READ "${report}" json)
    string(JSON tests GET "${json}" tests)
    string(JSON failures GET "${json}" failures)
    if(tests LESS 1)
        fail("no test ran")
    endif()
    if(NOT failures EQUAL tests)
        fail("${failures} of ${tests} tests failed, all of them should:\n${out}")
    endif()
    message(STATUS "${failures} of ${tests} tests fail, as they should")
elseif(MT_MODE STREQUAL "abort")
    execute_process(COMMAND "${binary}" --gtest_list_tests RESULT_VARIABLE code OUTPUT_VARIABLE listing)
    set(suite "")
    set(ran 0)
    string(REPLACE "\n" ";" lines "${listing}")
    foreach(line IN LISTS lines)
        if(line MATCHES "^([A-Za-z0-9_/]+)\\.$")
            set(suite "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^  ([A-Za-z0-9_/]+)")
            execute_process(COMMAND "${binary}" "--gtest_filter=${suite}.${CMAKE_MATCH_1}"
                            RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
            if(code EQUAL 0)
                fail("${suite}.${CMAKE_MATCH_1} passed, it must not")
            endif()
            math(EXPR ran "${ran} + 1")
        endif()
    endforeach()
    if(ran LESS 1)
        fail("no test ran")
    endif()
    message(STATUS "${ran} tests die, as they should")
else()
    fail("MT_MODE must be throw or abort, not ${MT_MODE}")
endif()

# ---- 5. idempotence -------------------------------------------------------------------------------------------
# --check exits 4 if any file would be created, rewritten or given a .new; the
# warnings of the analysis are printed again, and are not what is being asked.
run("gen-impl --check (idempotence)" 0 "${MT_BIN}" ${args} --check)
message(STATUS "round trip passed")
