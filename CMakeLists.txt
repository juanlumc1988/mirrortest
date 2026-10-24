# SPDX-License-Identifier: Apache-2.0
# Part of mirrortest -- bidirectional C++ test/code generator.

cmake_minimum_required(VERSION 3.20)
project(mirrortest VERSION 0.1.0 LANGUAGES C CXX)

option(MT_BUILD_TESTS "Build the unit, golden and round-trip tests" ON)
option(MT_WERROR      "Treat warnings as errors" OFF)

# The generator itself is C++17 throughout. It runs on the development host,
# never on the target, so there is nothing to gain from a lower standard.
# What it *emits* is a different matter -- that is governed by --std and
# defaults to code that is valid C++11 (see docs/design.md).

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MtDependencies)
include(MtEmbedFiles)

set(MT_WARNINGS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(MT_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast)
    if(MT_WERROR)
        list(APPEND MT_WARNINGS -Werror)
    endif()
elseif(MSVC)
    set(MT_WARNINGS /W4 /permissive-)
    if(MT_WERROR)
        list(APPEND MT_WARNINGS /WX)
    endif()
endif()

function(mt_configure tgt)
    set_target_properties(${tgt} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_compile_options(${tgt} PRIVATE ${MT_WARNINGS})
endfunction()

# ---- core library: IR, parsing, inference, emission -----------------------

# The default templates, compiled in (cmake/MtEmbedFiles.cmake).
set(MT_EMBEDDED_TEMPLATES "${CMAKE_CURRENT_BINARY_DIR}/mt_embedded_templates.cpp")
mt_embed_templates("${MT_EMBEDDED_TEMPLATES}" "${CMAKE_CURRENT_SOURCE_DIR}/templates")

add_library(mt_core STATIC
    src/backend/cxx_format.cpp
    src/backend/gtest_backend.cpp
    src/backend/impl_backend.cpp
    src/backend/impl_merge.cpp
    src/backend/test_planner.cpp
    src/cli/gen_impl.cpp
    src/cli/gen_tests.cpp
    src/cli/plan_collector.cpp
    src/cli/run.cpp
    src/core/config.cpp
    src/core/diagnostics.cpp
    src/core/diff.cpp
    src/core/include_search.cpp
    src/core/merge.cpp
    src/core/naming.cpp
    src/core/regions.cpp
    src/core/source.cpp
    src/core/templates.cpp
    src/core/writer.cpp
    src/frontend/annotations.cpp
    src/frontend/classify.cpp
    src/frontend/code_frontend.cpp
    src/frontend/facts.cpp
    src/frontend/known_names.cpp
    src/frontend/inference.cpp
    src/frontend/literals.cpp
    src/frontend/pipeline.cpp
    src/frontend/test_frontend.cpp
    src/ir/classify.cpp
    src/ir/type.cpp
    src/syntax/extract.cpp
    src/syntax/preprocess.cpp
    src/syntax/tree.cpp
    "${MT_EMBEDDED_TEMPLATES}")
target_include_directories(mt_core PUBLIC include)
target_link_libraries(mt_core
    PUBLIC  mt::tree_sitter nlohmann_json::nlohmann_json
    PRIVATE mt::inja CLI11::CLI11)
set_source_files_properties(src/cli/run.cpp PROPERTIES
    COMPILE_DEFINITIONS MT_VERSION="${PROJECT_VERSION}")
mt_configure(mt_core)

# ---- command-line tool -----------------------------------------------------

add_executable(mirrortest apps/cli/main.cpp)
target_link_libraries(mirrortest PRIVATE mt_core)
mt_configure(mirrortest)

# ---- tests -----------------------------------------------------------------

if(MT_BUILD_TESTS)
    enable_testing()
    include(GoogleTest)

    add_executable(mt_unit_tests
        tests/unit/test_annotations.cpp
        tests/unit/test_classify.cpp
        tests/unit/test_cli.cpp
        tests/unit/test_code_frontend.cpp
        tests/unit/test_config.cpp
        tests/unit/test_cxx_format.cpp
        tests/unit/test_dependencies.cpp
        tests/unit/test_extract.cpp
        tests/unit/test_gen_impl.cpp
        tests/unit/test_gen_tests.cpp
        tests/unit/test_gtest_backend.cpp
        tests/unit/test_include_search.cpp
        tests/unit/test_impl_backend.cpp
        tests/unit/test_impl_merge.cpp
        tests/unit/test_inference.cpp
        tests/unit/test_ir_classify.cpp
        tests/unit/test_known_names.cpp
        tests/unit/test_literals.cpp
        tests/unit/test_diagnostics.cpp
        tests/unit/test_diff.cpp
        tests/unit/test_merge.cpp
        tests/unit/test_naming.cpp
        tests/unit/test_pipeline.cpp
        tests/unit/test_preprocess.cpp
        tests/unit/test_regions.cpp
        tests/unit/test_source.cpp
        tests/unit/test_syntax_tree.cpp
        tests/unit/test_test_frontend.cpp
        tests/unit/test_test_planner.cpp
        tests/unit/test_templates.cpp
        tests/unit/test_writer.cpp
        tests/unit/test_type.cpp)
    target_link_libraries(mt_unit_tests PRIVATE
        mt_core mt::inja nlohmann_json::nlohmann_json GTest::gtest_main)
    mt_configure(mt_unit_tests)
    target_compile_definitions(mt_unit_tests PRIVATE
        MT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/data")
    gtest_discover_tests(mt_unit_tests DISCOVERY_MODE PRE_TEST)

    # Golden files: every case under tests/golden/ against the real command
    # line, plus tests of the runner itself (tests/golden/golden.h).
    #   ctest -R golden                      check
    #   cmake --build build -t update-golden rewrite the expectations
    # Review the diff of what that changes before committing it.
    add_executable(mt_golden_tests
        tests/golden/golden.cpp
        tests/golden/test_golden.cpp
        tests/golden/test_golden_runner.cpp)
    target_include_directories(mt_golden_tests PRIVATE tests/unit)
    target_link_libraries(mt_golden_tests PRIVATE mt_core GTest::gtest_main)
    target_compile_definitions(mt_golden_tests PRIVATE
        MT_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")
    mt_configure(mt_golden_tests)
    gtest_discover_tests(mt_golden_tests TEST_PREFIX golden. DISCOVERY_MODE PRE_TEST)
    add_custom_target(update-golden
        COMMAND "${CMAKE_COMMAND}" -E env MT_UPDATE_GOLDEN=1 $<TARGET_FILE:mt_golden_tests>
                --gtest_filter=All/GoldenCase.*
        DEPENDS mt_golden_tests
        COMMENT "Rewriting the golden expectations from the current output"
        VERBATIM)

    # cmake/MtEmbedFiles.cmake, in isolation: awkward content survives, CRLF is
    # normalised, an unchanged embedding does not rewrite its output, and a
    # template containing the raw-string terminator fails the configure.
    foreach(case ok clash)
        add_test(NAME cmake.embed_${case}
            COMMAND "${CMAKE_COMMAND}"
                -S "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/embed"
                -B "${CMAKE_CURRENT_BINARY_DIR}/cmake-embed-${case}"
                -DMT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DCASE=${case})
    endforeach()
    set_tests_properties(cmake.embed_ok PROPERTIES PASS_REGULAR_EXPRESSION "embedding checks passed")
    set_tests_properties(cmake.embed_clash PROPERTIES PASS_REGULAR_EXPRESSION "raw-string terminator")

    # gen-impl round trips (docs/design.md 16.3): generate from the test of a
    # golden case, compile the stubs as strict embedded C++11, compile and run
    # the test, require every test red, and require that a second run changes
    # nothing. Driven by compiler command lines, not a nested CMake project, so
    # the cases are fast and need no network; GoogleTest is the main build's.
    if(NOT MSVC)
        foreach(case basic_class fixture_members namespaces multi_class_includes no_exceptions)
            set(mode throw)
            if(case STREQUAL "no_exceptions")
                set(mode abort)
            endif()
            add_test(NAME roundtrip.${case}
                COMMAND "${CMAKE_COMMAND}"
                    -DMT_BIN=$<TARGET_FILE:mirrortest>
                    -DMT_CASE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen-impl/${case}
                    -DMT_WORK=${CMAKE_CURRENT_BINARY_DIR}/roundtrip/${case}
                    -DMT_MODE=${mode}
                    -DMT_CXX=${CMAKE_CXX_COMPILER}
                    "-DMT_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
                    "-DMT_GTEST_INCLUDE=$<TARGET_PROPERTY:GTest::gtest,INTERFACE_INCLUDE_DIRECTORIES>"
                    "-DMT_GTEST_LIBS=$<TARGET_FILE:GTest::gtest_main>$<SEMICOLON>$<TARGET_FILE:GTest::gtest>"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/run_case.cmake")
            set_tests_properties(roundtrip.${case} PROPERTIES TIMEOUT 300)
        endforeach()
    endif()

    # gen-tests round trips (docs/design.md 16.3): generate tests from the header
    # of a golden case, compile an example implementation of it as strict C++11
    # and the tests as a project would (-Wall -Wextra -Werror), link and run: every
    # test must fail (or, with --placeholder skip, be skipped). A case is a .cpp
    # in tests/roundtrip/gen-tests/ named after the golden case, plus an optional
    # <case>.prelude.h forced into every translation unit for the types the header
    # relies on and the tool cannot know.
    if(NOT MSVC)
        file(GLOB mt_gen_tests_impls RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/gen-tests"
             "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/gen-tests/*.cpp")
        list(SORT mt_gen_tests_impls)
        foreach(impl IN LISTS mt_gen_tests_impls)
            string(REGEX REPLACE "\\.cpp$" "" case "${impl}")
            set(case_dir "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen-tests/${case}")
            file(READ "${case_dir}/args.txt" case_args)
            set(expect fail)
            if(case_args MATCHES "\n--placeholder\nskip\n")
                set(expect skip)
            endif()
            set(prelude "")
            if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/gen-tests/${case}.prelude.h")
                set(prelude "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/gen-tests/${case}.prelude.h")
            endif()
            add_test(NAME roundtrip.gen_tests.${case}
                COMMAND "${CMAKE_COMMAND}"
                    -DMT_BIN=$<TARGET_FILE:mirrortest>
                    -DMT_CASE_DIR=${case_dir}
                    -DMT_IMPL=${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/gen-tests/${impl}
                    "-DMT_PRELUDE=${prelude}"
                    -DMT_WORK=${CMAKE_CURRENT_BINARY_DIR}/roundtrip/gen-tests/${case}
                    -DMT_EXPECT=${expect}
                    -DMT_CXX=${CMAKE_CXX_COMPILER}
                    "-DMT_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
                    "-DMT_GTEST_INCLUDE=$<TARGET_PROPERTY:GTest::gtest,INTERFACE_INCLUDE_DIRECTORIES>"
                    "-DMT_GTEST_LIBS=$<TARGET_FILE:GTest::gtest_main>$<SEMICOLON>$<TARGET_FILE:GTest::gtest>"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/run_gen_tests_case.cmake")
            set_tests_properties(roundtrip.gen_tests.${case} PROPERTIES TIMEOUT 300)
        endforeach()
    endif()

    # The merge round trip: first run, implement a method, grow the test, merge;
    # the work survives and passes, the new stubs are red.
    if(NOT MSVC)
        add_test(NAME roundtrip.merge
            COMMAND "${CMAKE_COMMAND}"
                -DMT_BIN=$<TARGET_FILE:mirrortest>
                -DMT_WORK=${CMAKE_CURRENT_BINARY_DIR}/roundtrip/merge
                -DMT_V1=${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/merge/foo_test.v1.cpp
                -DMT_V2=${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/merge/foo_test.v2.cpp
                "-DMT_PATCH_FROM=throw std::logic_error(\"mirrortest: not implemented: Foo::size()\");"
                "-DMT_PATCH_TO=return 3u;"
                "-DMT_PASS=FooTest.Size"
                "-DMT_FAIL=FooTest.More"
                -DMT_CXX=${CMAKE_CXX_COMPILER}
                "-DMT_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
                "-DMT_GTEST_INCLUDE=$<TARGET_PROPERTY:GTest::gtest,INTERFACE_INCLUDE_DIRECTORIES>"
                "-DMT_GTEST_LIBS=$<TARGET_FILE:GTest::gtest_main>$<SEMICOLON>$<TARGET_FILE:GTest::gtest>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/run_merge.cmake")
        set_tests_properties(roundtrip.merge PROPERTIES TIMEOUT 300)
    endif()

    # Do the stubs of *every* gen-impl golden compile as strict C++11? With the
    # host compiler always, and with MT_ARM_CXX (a cross compiler, e.g.
    # arm-linux-gnueabihf-g++, which CI installs) for the target they are for.
    # Syntax only: nothing is linked or run.
    set(MT_ARM_CXX "" CACHE STRING "Cross compiler for the stub syntax checks (empty: skip them)")
    file(GLOB mt_golden_impl_cases LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen-impl"
         "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen-impl/*")
    list(SORT mt_golden_impl_cases)
    foreach(case IN LISTS mt_golden_impl_cases)
        set(case_dir "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen-impl/${case}")
        # Only the cases that succeed and write a source file have stubs to check.
        file(GLOB_RECURSE case_sources "${case_dir}/expected/*.cpp")
        if(EXISTS "${case_dir}/exit_code.txt" OR NOT case_sources)
            continue()
        endif()
        foreach(kind host arm)
            if(kind STREQUAL "host")
                set(compiler "${CMAKE_CXX_COMPILER}")
            else()
                set(compiler "${MT_ARM_CXX}")
            endif()
            if(compiler)
                add_test(NAME stubs.${kind}.${case}
                    COMMAND "${CMAKE_COMMAND}"
                        -DMT_BIN=$<TARGET_FILE:mirrortest>
                        -DMT_CASE_DIR=${case_dir}
                        -DMT_WORK=${CMAKE_CURRENT_BINARY_DIR}/stubs-${kind}/${case}
                        -DMT_CXX=${compiler}
                        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/syntax_case.cmake")
                set_tests_properties(stubs.${kind}.${case} PROPERTIES TIMEOUT 120)
            endif()
        endforeach()
    endforeach()

    # The CLI exists and answers. Behavioural CLI coverage lives in the golden
    # and round-trip suites, which drive the real binary.
    add_test(NAME cli.version COMMAND mirrortest --version)
    set_tests_properties(cli.version PROPERTIES PASS_REGULAR_EXPRESSION "mirrortest ${PROJECT_VERSION}")
endif()
