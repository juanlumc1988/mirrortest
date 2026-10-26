# SPDX-License-Identifier: Apache-2.0
# Part of mirrortest -- bidirectional C++ test/code generator.
#
# One Qt Test round trip (docs/design.md, section 16.3), run as
#
#   cmake -DMT_DIRECTION=gen-tests|gen-impl -DMT_BIN=... -DMT_CASE_DIR=... -DMT_WORK=... -P run_qt_case.cmake
#
# The questions are the ones of the GoogleTest round trips, asked of Qt Test:
#
#   gen-tests   do the skeletons compile against the real API, and does every
#               slot stay red (or, with --placeholder skip, skipped) until
#               somebody writes it?
#   gen-impl    does what is generated make the Qt test compile, and does every
#               slot then fail?
#
# Qt Test needs more than a compiler: moc turns the test class (and any class
# of the header under test that has Q_OBJECT) into code, and the AUTOMOC rule
# that the test's `#include "tst_foo.moc"` finds moc's output is what the build
# does with -I. Each test file is its own program (every one has a main).
#
#   gen-tests: generate; moc the Q_OBJECT headers of the case and build them;
#              build the example implementation (strict, C++17: Qt 6 needs it);
#              per generated file: moc it, compile it with -Wall -Wextra -Werror,
#              link, list its slots with -functions, run it and read the verdicts
#   gen-impl:  generate; build the stubs (strict); moc and compile the test;
#              link; run each slot alone and require it to fail (R26: after the
#              first exception QTest rethrows, so a slot that runs after another
#              would never be seen) and the whole program to fail
#
# and, in both, a second run with --check changes nothing.
#
# Inputs (-D):
#   MT_DIRECTION   gen-tests or gen-impl
#   MT_BIN         the mirrortest executable
#   MT_CASE_DIR    tests/golden/<direction>/<case>: its args.txt and input/ are used
#   MT_IMPL        gen-tests: the example implementation (a .cpp)
#   MT_PRELUDE     optional: a header forced into every translation unit
#   MT_WORK        scratch directory, emptied first
#   MT_CXX         the C++ compiler
#   MT_CXX_FLAGS   flags shared with the main build (sanitizers, for one)
#   MT_QT_INCLUDE  Qt's include directories (a list: QtCore, QtTest, the root)
#   MT_QT_LIBS     Qt's libraries (a list: QtTest, QtCore)
#   MT_MOC         the moc executable
#   MT_EXPECT      gen-tests: "fail" (default) or "skip"

cmake_minimum_required(VERSION 3.20)

foreach(var MT_DIRECTION MT_BIN MT_CASE_DIR MT_WORK MT_CXX MT_QT_INCLUDE MT_QT_LIBS MT_MOC)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "run_qt_case.cmake: -D${var}=... is required")
    endif()
endforeach()
if(NOT MT_DIRECTION STREQUAL "gen-tests" AND NOT MT_DIRECTION STREQUAL "gen-impl")
    message(FATAL_ERROR "run_qt_case.cmake: MT_DIRECTION must be gen-tests or gen-impl, not ${MT_DIRECTION}")
endif()
if(MT_DIRECTION STREQUAL "gen-tests" AND (NOT DEFINED MT_IMPL OR "${MT_IMPL}" STREQUAL ""))
    message(FATAL_ERROR "run_qt_case.cmake: -DMT_IMPL=... is required for gen-tests")
endif()
if(NOT DEFINED MT_EXPECT)
    set(MT_EXPECT fail)
endif()

set(input "${MT_CASE_DIR}/input")
set(gen "${MT_WORK}/gen")
set(moc_dir "${MT_WORK}/moc")
file(REMOVE_RECURSE "${MT_WORK}")
file(MAKE_DIRECTORY "${gen}" "${moc_dir}")

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

# ---- generate ------------------------------------------------------------------------------------------------
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
    if(arg MATCHES "\\.cpp$")
        set(test_file "${arg}")
    endif()
    set(previous "${arg}")
endforeach()

run("${MT_DIRECTION}" 0 "${MT_BIN}" ${args})

# ---- the flags -----------------------------------------------------------------------------------------------
separate_arguments(shared UNIX_COMMAND "${MT_CXX_FLAGS}")
set(qt_includes "")
foreach(dir IN LISTS MT_QT_INCLUDE)
    list(APPEND qt_includes "-I${dir}")
endforeach()
list(REMOVE_DUPLICATES qt_includes)
set(prelude "")
if(DEFINED MT_PRELUDE AND NOT "${MT_PRELUDE}" STREQUAL "")
    set(prelude -include "${MT_PRELUDE}")
endif()
# Qt 6 needs C++17 and a position independent build; Qt 5 does not mind either.
set(qt_flags -std=c++17 -fPIC)

# ---- moc: the classes of the headers under test that have Q_OBJECT ---------------------------------------
set(objects "")
set(moc_index 0)
file(GLOB_RECURSE headers "${input}/*.h" "${input}/*.hpp")
list(SORT headers)
foreach(header IN LISTS headers)
    file(READ "${header}" header_text)
    if(header_text MATCHES "Q_OBJECT")
        set(moc_source "${MT_WORK}/moc_header${moc_index}.cpp")
        set(moc_object "${MT_WORK}/moc_header${moc_index}.o")
        run("moc ${header}" 0 "${MT_MOC}" ${include_dirs} "${header}" -o "${moc_source}")
        # moc's own code is not ours to keep warning-free.
        run("compiling moc's ${header}" 0 "${MT_CXX}" ${shared} ${qt_flags} -w ${prelude} ${include_dirs} ${qt_includes}
            -c "${moc_source}" -o "${moc_object}")
        list(APPEND objects "${moc_object}")
        math(EXPR moc_index "${moc_index} + 1")
    endif()
endforeach()

# ---- one program from one Qt test file -------------------------------------------------------------------------
# Runs the program's functions and checks the verdicts. `kind` is fail, skip or die.
function(check_program binary kind)
    execute_process(COMMAND "${binary}" -functions RESULT_VARIABLE code OUTPUT_VARIABLE listing ERROR_VARIABLE err)
    if(NOT code EQUAL 0)
        fail("${binary} -functions: exit ${code}\n${listing}${err}")
    endif()
    string(REPLACE "\n" ";" lines "${listing}")
    set(functions "")
    foreach(line IN LISTS lines)
        # A `<name>_data` slot is not a test: it fills the table of <name>, which is what shows as `name(row)`.
        if(line MATCHES "^([A-Za-z0-9_]+)\\(\\)$")
            set(name "${CMAKE_MATCH_1}")
            if(NOT name MATCHES "_data$")
                list(APPEND functions "${name}")
            endif()
        endif()
    endforeach()
    list(LENGTH functions count)
    if(count LESS 1)
        fail("${binary} has no test function:\n${listing}")
    endif()

    if(kind STREQUAL "die")
        # Stubs that throw: QTest reports the failure and rethrows, so the program ends at the first slot.
        # Each slot alone must fail, and so must all of them together.
        foreach(function IN LISTS functions)
            execute_process(COMMAND "${binary}" "${function}" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
            if(code EQUAL 0)
                fail("${function} passed, it must not:\n${out}${err}")
            endif()
            if(out MATCHES "PASS   : [A-Za-z0-9_:]*::${function}\\([^)]*\\)")
                fail("${function} is reported as passed:\n${out}")
            endif()
        endforeach()
        execute_process(COMMAND "${binary}" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(code EQUAL 0)
            fail("the program passed, it must fail:\n${out}")
        endif()
        message(STATUS "${count} slots die or fail, alone and together, as they should")
        return()
    endif()

    execute_process(COMMAND "${binary}" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(kind STREQUAL "fail")
        if(code EQUAL 0)
            fail("the program passed, it must fail:\n${out}")
        endif()
        foreach(function IN LISTS functions)
            if(NOT out MATCHES "FAIL!  : [A-Za-z0-9_:]*::${function}\\([^)]*\\)")
                fail("${function} did not fail:\n${out}")
            endif()
            if(out MATCHES "PASS   : [A-Za-z0-9_:]*::${function}\\([^)]*\\)")
                fail("${function} is reported as passed:\n${out}")
            endif()
        endforeach()
        message(STATUS "${count} slots fail, as they should")
    else()
        if(NOT code EQUAL 0)
            fail("a skipped slot is not a failure, but the program exited with ${code}:\n${out}${err}")
        endif()
        foreach(function IN LISTS functions)
            if(NOT out MATCHES "SKIP   : [A-Za-z0-9_:]*::${function}\\([^)]*\\)")
                fail("${function} was not skipped:\n${out}")
            endif()
        endforeach()
        message(STATUS "${count} slots are skipped, as they should")
    endif()
endfunction()

# Builds and checks the program of one test file; `extra_objects` are linked in.
function(build_and_check test kind extra_objects index)
    get_filename_component(stem "${test}" NAME_WE)
    set(moc_file "${moc_dir}/${stem}.moc")
    run("moc ${test}" 0 "${MT_MOC}" ${include_dirs} "${test}" -o "${moc_file}")
    set(test_object "${MT_WORK}/test${index}.o")
    # QVERIFY_EXCEPTION_THROWN is deprecated since Qt 6.3 and the test of that case uses it
    # on purpose: a warning of Qt's, in the user's test, is not what is being asked.
    run("compiling ${test}" 0 "${MT_CXX}" ${shared} ${qt_flags} -Wall -Wextra -Werror -Wno-error=deprecated-declarations
        ${prelude} "-I${moc_dir}" ${include_dirs} ${qt_includes} -c "${test}" -o "${test_object}")
    set(binary "${MT_WORK}/program${index}")
    run("linking ${test}" 0 "${MT_CXX}" ${shared} "${test_object}" ${extra_objects} ${MT_QT_LIBS} -o "${binary}")
    check_program("${binary}" "${kind}")
endfunction()

if(MT_DIRECTION STREQUAL "gen-tests")
    # The example implementation, strict.
    set(impl_object "${MT_WORK}/impl.o")
    run("compiling ${MT_IMPL}" 0 "${MT_CXX}" ${shared} ${qt_flags} -Wall -Wextra -Wpedantic -Werror
        ${prelude} ${include_dirs} ${qt_includes} -c "${MT_IMPL}" -o "${impl_object}")
    list(APPEND objects "${impl_object}")

    file(GLOB_RECURSE tests "${gen}/*.cpp")
    list(SORT tests)
    if(NOT tests)
        fail("gen-tests wrote no test file")
    endif()
    set(index 0)
    foreach(test IN LISTS tests)
        build_and_check("${test}" "${MT_EXPECT}" "${objects}" "${index}")
        math(EXPR index "${index} + 1")
    endforeach()
else()
    file(GLOB_RECURSE stubs "${gen}/*.cpp")
    list(SORT stubs)
    if(NOT stubs)
        fail("gen-impl wrote no source file")
    endif()
    set(index 0)
    foreach(stub IN LISTS stubs)
        set(object "${MT_WORK}/stub${index}.o")
        run("compiling ${stub}" 0 "${MT_CXX}" ${shared} ${qt_flags} -Wall -Wextra -Wpedantic -Werror
            ${prelude} "-I${gen}" ${qt_includes} -c "${stub}" -o "${object}")
        list(APPEND objects "${object}")
        math(EXPR index "${index} + 1")
    endforeach()
    if(NOT DEFINED test_file)
        fail("no *.cpp among the arguments of ${MT_CASE_DIR}/args.txt")
    endif()
    # The test includes the headers that were generated; moc finds them with the same -I.
    set(include_dirs "-I${gen}" ${include_dirs})
    build_and_check("${test_file}" die "${objects}" 99)
endif()

# ---- idempotence ------------------------------------------------------------------------------------------------
run("${MT_DIRECTION} --check (idempotence)" 0 "${MT_BIN}" ${args} --check)
message(STATUS "round trip passed")
