# SPDX-License-Identifier: Apache-2.0
# Part of mirrortest -- bidirectional C++ test/code generator.
#
# One gen-tests round trip (docs/design.md, section 16.3), run as
#
#   cmake -DMT_BIN=... -DMT_CASE_DIR=... -DMT_IMPL=... -DMT_WORK=... -P run_gen_tests_case.cmake
#
# The question it answers is the one a legacy codebase asks of the tool: do the
# skeletons it writes compile against the real API, and do they stay red until
# somebody writes them? Not "does it look right" (the golden files do that).
#
#   1. gen-tests over the header of a golden case, into <work>/gen
#   2. an example implementation of that header compiled under strict rules:
#      C++11, -Wall -Wextra -Wpedantic -Werror
#   3. every generated test file compiled as a project's tests are: C++14,
#      -Wall -Wextra -Werror -- so an unused variable in a skeleton is a failure
#   4. linked with GoogleTest and run: with the default placeholder every test
#      must fail, with --placeholder skip every test must be skipped and the
#      binary must pass
#   5. gen-tests again with --check: exit 0 means no file would change
#
# Inputs (-D):
#   MT_BIN           the mirrortest executable
#   MT_CASE_DIR      tests/golden/gen-tests/<case>: its args.txt and input/ are used
#   MT_IMPL          the example implementation (a .cpp)
#   MT_PRELUDE       optional: a header forced into every translation unit
#   MT_MOCKS         optional: a .cpp compiled as one more test file, with the output
#                    directory on -I, to check the generated mock headers (it defines
#                    no test: whether a mock can be built is a compile-time question)
#   MT_WORK          scratch directory, emptied first
#   MT_CXX           the C++ compiler
#   MT_CXX_FLAGS     flags shared with the main build (sanitizers, for one)
#   MT_GTEST_INCLUDE, MT_GTEST_LIBS   GoogleTest as the main build has it (lists)
#   MT_EXPECT        "fail" (default) or "skip"

cmake_minimum_required(VERSION 3.20)

foreach(var MT_BIN MT_CASE_DIR MT_IMPL MT_WORK MT_CXX MT_GTEST_INCLUDE MT_GTEST_LIBS)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "run_gen_tests_case.cmake: -D${var}=... is required")
    endif()
endforeach()
if(NOT DEFINED MT_EXPECT)
    set(MT_EXPECT fail)
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
set(include_dirs "-I${input}" "-I${input}/include")
set(previous "")
foreach(arg IN LISTS raw_args)
    string(REPLACE "\${IN}" "${input}" arg "${arg}")
    string(REPLACE "\${OUT}" "${gen}" arg "${arg}")
    list(APPEND args "${arg}")
    if(previous STREQUAL "-I")
        list(APPEND include_dirs "-I${arg}")
    endif()
    set(previous "${arg}")
endforeach()

run("gen-tests" 0 "${MT_BIN}" ${args})

# ---- the flags ------------------------------------------------------------------------------------------------
separate_arguments(shared UNIX_COMMAND "${MT_CXX_FLAGS}")
set(prelude "")
if(DEFINED MT_PRELUDE AND NOT "${MT_PRELUDE}" STREQUAL "")
    set(prelude -include "${MT_PRELUDE}")
endif()

# MT_GTEST_INCLUDE is a list: GoogleTest's target exports more than one directory.
set(gtest_includes "")
foreach(dir IN LISTS MT_GTEST_INCLUDE)
    list(APPEND gtest_includes "-I${dir}")
endforeach()

# ---- 2. the example implementation ---------------------------------------------------------------------------
set(impl_object "${MT_WORK}/impl.o")
run("compiling ${MT_IMPL}" 0 "${MT_CXX}" ${shared} -std=c++11 -Wall -Wextra -Wpedantic -Werror
    ${prelude} ${include_dirs} -c "${MT_IMPL}" -o "${impl_object}")

# ---- 3. the generated tests -----------------------------------------------------------------------------------
file(GLOB_RECURSE tests "${gen}/*.cpp")
list(SORT tests)
if(NOT tests)
    fail("gen-tests wrote no test file")
endif()
set(objects "${impl_object}")
set(index 0)
foreach(test IN LISTS tests)
    set(object "${MT_WORK}/test${index}.o")
    run("compiling ${test}" 0 "${MT_CXX}" ${shared} -std=c++14 -Wall -Wextra -Werror
        ${prelude} ${include_dirs} ${gtest_includes} -c "${test}" -o "${object}")
    list(APPEND objects "${object}")
    math(EXPR index "${index} + 1")
endforeach()

if(DEFINED MT_MOCKS AND NOT "${MT_MOCKS}" STREQUAL "")
    set(object "${MT_WORK}/mocks.o")
    run("compiling ${MT_MOCKS}" 0 "${MT_CXX}" ${shared} -std=c++14 -Wall -Wextra -Werror
        ${prelude} "-I${gen}" ${include_dirs} ${gtest_includes} -c "${MT_MOCKS}" -o "${object}")
    list(APPEND objects "${object}")
endif()

set(binary "${MT_WORK}/roundtrip")
run("linking" 0 "${MT_CXX}" ${shared} ${objects} ${MT_GTEST_LIBS} -pthread -o "${binary}")

# ---- 4. red, or skipped ---------------------------------------------------------------------------------------
# A test called Inherits, IsNotCopyable or IsNotMovable (design 9.1) is static_asserts and nothing
# left to write: compiling it was the check, and it passes. Every other test is a placeholder and
# must be red (or skipped).
set(structural_filter "*.Inherits:*.IsNotCopyable:*.IsNotMovable")
execute_process(COMMAND "${binary}" --gtest_list_tests "--gtest_filter=${structural_filter}" RESULT_VARIABLE code OUTPUT_VARIABLE listed ERROR_VARIABLE err)
if(NOT code EQUAL 0)
    fail("listing the structural tests: exit ${code}\n${listed}${err}")
endif()
string(REGEX MATCHALL "\n  (Inherits|IsNotCopyable|IsNotMovable)" structural_found "\n${listed}")
list(LENGTH structural_found structural)
if(structural GREATER 0)
    run("running the structural tests" 0 "${binary}" "--gtest_filter=${structural_filter}")
endif()

if(MT_EXPECT STREQUAL "fail")
    set(report "${MT_WORK}/report.json")
    execute_process(COMMAND "${binary}" "--gtest_output=json:${report}" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT EXISTS "${report}")
        fail("no JSON report (did the binary crash?):\n${out}${err}")
    endif()
    file(READ "${report}" json)
    string(JSON total GET "${json}" tests)
    string(JSON failures GET "${json}" failures)
    if(total LESS 1)
        fail("no test ran")
    endif()
    math(EXPR written "${total} - ${structural}")
    if(written GREATER 0 AND code EQUAL 0)
        fail("the test binary passed, it must fail:\n${out}")
    endif()
    if(NOT failures EQUAL written)
        fail("${failures} of ${total} tests failed, ${written} should (${structural} only check the structure):\n${out}")
    endif()
    message(STATUS "${failures} of ${total} tests fail, as they should")
elseif(MT_EXPECT STREQUAL "skip")
    execute_process(COMMAND "${binary}" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT code EQUAL 0)
        fail("a skipped test is not a failure, but the binary exited with ${code}:\n${out}${err}")
    endif()
    if(NOT out MATCHES "\\[==========\\] ([0-9]+) tests? from")
        fail("cannot tell how many tests ran:\n${out}")
    endif()
    set(total "${CMAKE_MATCH_1}")
    math(EXPR written "${total} - ${structural}")
    if(written GREATER 0)
        if(NOT out MATCHES "\\[  SKIPPED \\] ([0-9]+) tests?, listed below")
            fail("no test was skipped:\n${out}")
        endif()
        set(skipped "${CMAKE_MATCH_1}")
    else()
        set(skipped 0)
    endif()
    if(total LESS 1)
        fail("no test ran")
    endif()
    if(NOT skipped EQUAL written)
        fail("${skipped} of ${total} tests were skipped, ${written} should be (${structural} only check the structure):\n${out}")
    endif()
    message(STATUS "${skipped} of ${total} tests are skipped, as they should")
else()
    fail("MT_EXPECT must be fail or skip, not ${MT_EXPECT}")
endif()

# ---- 5. idempotence -------------------------------------------------------------------------------------------
run("gen-tests --check (idempotence)" 0 "${MT_BIN}" ${args} --check)
message(STATUS "round trip passed")
