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
    src/cli/commands.cpp
    src/cli/run.cpp
    src/core/config.cpp
    src/core/diagnostics.cpp
    src/core/diff.cpp
    src/core/naming.cpp
    src/core/source.cpp
    src/core/templates.cpp
    src/core/writer.cpp
    src/frontend/classify.cpp
    src/frontend/code_frontend.cpp
    src/frontend/facts.cpp
    src/frontend/known_names.cpp
    src/frontend/literals.cpp
    src/frontend/test_frontend.cpp
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
        tests/unit/test_classify.cpp
        tests/unit/test_cli.cpp
        tests/unit/test_code_frontend.cpp
        tests/unit/test_config.cpp
        tests/unit/test_dependencies.cpp
        tests/unit/test_extract.cpp
        tests/unit/test_known_names.cpp
        tests/unit/test_literals.cpp
        tests/unit/test_diagnostics.cpp
        tests/unit/test_diff.cpp
        tests/unit/test_naming.cpp
        tests/unit/test_preprocess.cpp
        tests/unit/test_source.cpp
        tests/unit/test_syntax_tree.cpp
        tests/unit/test_test_frontend.cpp
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

    # The CLI exists and answers. Behavioural CLI coverage lives in the golden
    # and round-trip suites, which drive the real binary.
    add_test(NAME cli.version COMMAND mirrortest --version)
    set_tests_properties(cli.version PROPERTIES PASS_REGULAR_EXPRESSION "mirrortest ${PROJECT_VERSION}")
endif()
