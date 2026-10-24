# SPDX-License-Identifier: Apache-2.0
# Part of mirrortest -- bidirectional C++ test/code generator.
#
# The merge round trip (docs/design.md, section 16.3): the life of a TDD session.
#
#   1. gen-impl over the first version of a test        -> stubs that make it fail
#   2. the programmer implements one method              -> its test passes
#   3. the test grows (a second test, new methods)
#   4. gen-impl --overwrite merge                        -> the new stubs appear beside the work
#   5. everything compiles as strict C++11 / C++14, and when it runs
#      the implemented test still passes and the new one fails.
#
# Run as  cmake -DMT_BIN=... -DMT_WORK=... -DMT_V1=... -DMT_V2=... \
#               -DMT_PATCH_FROM=... -DMT_PATCH_TO=... -P run_merge.cmake
#
#   MT_V1, MT_V2     the two versions of the test (foo_test.cpp, including "foo.h")
#   MT_PATCH_FROM/TO the programmer's work: text in the generated foo.cpp, replaced
#   MT_PASS, MT_FAIL the tests (Suite.Name, ;-separated) that must pass / fail at the end
# plus MT_CXX, MT_CXX_FLAGS, MT_GTEST_INCLUDE, MT_GTEST_LIBS as for run_case.cmake.

cmake_minimum_required(VERSION 3.20)

foreach(var MT_BIN MT_WORK MT_V1 MT_V2 MT_PATCH_FROM MT_PATCH_TO MT_PASS MT_FAIL MT_CXX MT_GTEST_INCLUDE MT_GTEST_LIBS)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "run_merge.cmake: -D${var}=... is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${MT_WORK}")
file(MAKE_DIRECTORY "${MT_WORK}/tests")

# file(COPY_FILE) needs CMake 3.21; the project asks for 3.20.
function(put_test version)
    file(READ "${version}" text)
    file(WRITE "${MT_WORK}/tests/foo_test.cpp" "${text}")
endfunction()
put_test("${MT_V1}")

function(fail message)
    message(FATAL_ERROR "merge round trip failed: ${message}")
endfunction()

function(run description expected)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${MT_WORK}" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT "${code}" STREQUAL "${expected}")
        fail("${description}: exit ${code}, expected ${expected}\n${out}${err}")
    endif()
    set(last_err "${err}" PARENT_SCOPE)
endfunction()

# 1. the first run
run("gen-impl (first)" 0 "${MT_BIN}" gen-impl tests/foo_test.cpp --out src)

# 2. the programmer's work
file(READ "${MT_WORK}/src/foo.cpp" source)
string(FIND "${source}" "${MT_PATCH_FROM}" at)
if(at EQUAL -1)
    fail("the generated foo.cpp has no '${MT_PATCH_FROM}' to replace:\n${source}")
endif()
string(REPLACE "${MT_PATCH_FROM}" "${MT_PATCH_TO}" source "${source}")
file(WRITE "${MT_WORK}/src/foo.cpp" "${source}")

# 3 and 4. the test grows; merge
put_test("${MT_V2}")
run("gen-impl --overwrite merge" 0 "${MT_BIN}" gen-impl tests/foo_test.cpp --out src --overwrite merge)
if(EXISTS "${MT_WORK}/src/foo.h.new" OR EXISTS "${MT_WORK}/src/foo.cpp.new")
    fail("the merge fell back to a .new file")
endif()
file(READ "${MT_WORK}/src/foo.cpp" merged)
string(FIND "${merged}" "${MT_PATCH_TO}" kept)
if(kept EQUAL -1)
    fail("the programmer's work is gone from foo.cpp after the merge:\n${merged}")
endif()

# 5. it compiles, and the work counts
separate_arguments(shared UNIX_COMMAND "${MT_CXX_FLAGS}")
run("compiling foo.cpp" 0 "${MT_CXX}" ${shared} -std=c++11 -Wall -Wextra -Wpedantic -Werror -Isrc -c src/foo.cpp -o foo.o)
set(gtest_includes "")
foreach(dir IN LISTS MT_GTEST_INCLUDE)
    list(APPEND gtest_includes "-I${dir}")
endforeach()
run("compiling the test" 0 "${MT_CXX}" ${shared} -std=c++14 -Wall -Wextra -Werror -Isrc ${gtest_includes} -c tests/foo_test.cpp -o test.o)
run("linking" 0 "${MT_CXX}" ${shared} test.o foo.o ${MT_GTEST_LIBS} -pthread -o roundtrip)

execute_process(COMMAND "${MT_WORK}/roundtrip" "--gtest_output=json:${MT_WORK}/report.json" WORKING_DIRECTORY "${MT_WORK}"
                RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT EXISTS "${MT_WORK}/report.json")
    fail("no JSON report:\n${out}${err}")
endif()
file(READ "${MT_WORK}/report.json" json)

# The result of one test from GoogleTest's JSON: a failed test has a "failures" array.
function(test_failed name result_variable)
    string(REPLACE "." ";" parts "${name}")
    list(GET parts 0 suite)
    list(GET parts 1 test)
    string(JSON suites LENGTH "${json}" testsuites)
    math(EXPR last_suite "${suites} - 1")
    foreach(i RANGE ${last_suite})
        string(JSON suite_name GET "${json}" testsuites ${i} name)
        if(NOT suite_name STREQUAL suite)
            continue()
        endif()
        string(JSON count LENGTH "${json}" testsuites ${i} testsuite)
        math(EXPR last_test "${count} - 1")
        foreach(j RANGE ${last_test})
            string(JSON test_name GET "${json}" testsuites ${i} testsuite ${j} name)
            if(NOT test_name STREQUAL test)
                continue()
            endif()
            string(JSON failures ERROR_VARIABLE none GET "${json}" testsuites ${i} testsuite ${j} failures)
            if(none STREQUAL "NOTFOUND")
                set(${result_variable} TRUE PARENT_SCOPE)
            else()
                set(${result_variable} FALSE PARENT_SCOPE)
            endif()
            return()
        endforeach()
    endforeach()
    fail("${name} did not run")
endfunction()

foreach(name IN LISTS MT_PASS)
    test_failed("${name}" failed)
    if(failed)
        fail("${name} should pass: the programmer implemented what it needs")
    endif()
endforeach()
foreach(name IN LISTS MT_FAIL)
    test_failed("${name}" failed)
    if(NOT failed)
        fail("${name} should fail: its stubs are new")
    endif()
endforeach()
message(STATUS "merge round trip passed")
