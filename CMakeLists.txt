# SPDX-License-Identifier: Apache-2.0
# Part of mirrortest -- bidirectional C++ test/code generator.

cmake_minimum_required(VERSION 3.20)
project(mirrortest VERSION 0.1.0 LANGUAGES C CXX)

option(MT_BUILD_TESTS "Build the unit, golden and round-trip tests" ON)
option(MT_WERROR      "Treat warnings as errors" OFF)
option(MT_WITH_YAML   "Read configuration files written in YAML (.yaml, .yml); needs yaml-cpp" ON)
option(MT_BUILD_TOOLS "Build the development tools (tools/): the model dumper that tools/corpus.sh uses" OFF)
option(MT_WITH_LIBCLANG "Add the libclang frontend for headers (--frontend libclang); needs libclang" OFF)

# The generator itself is C++17 throughout. It runs on the development host,
# never on the target, so there is nothing to gain from a lower standard.
# What it *emits* is a different matter -- that is governed by --std and
# defaults to code that is valid C++11 (see docs/design.md).

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MtDependencies)
include(MtEmbedFiles)
if(MT_WITH_LIBCLANG)
    include(MtLibclang)
endif()

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
    src/backend/hook_header.cpp
    src/backend/impl_backend.cpp
    src/backend/impl_merge.cpp
    src/backend/mock_planner.cpp
    src/backend/qtest_backend.cpp
    src/backend/nested_expand.cpp
    src/backend/template_expand.cpp
    src/backend/test_planner.cpp
    src/backend/test_render.cpp
    src/cli/gen_impl.cpp
    src/cli/gen_tests.cpp
    src/cli/plan_collector.cpp
    src/cli/run.cpp
    src/core/compile_commands.cpp
    src/core/config.cpp
    src/core/config_yaml.cpp
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
    src/frontend/clang_frontend.cpp
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
    src/syntax/mock_macros.cpp
    src/syntax/preprocess.cpp
    src/syntax/tree.cpp
    "${MT_EMBEDDED_TEMPLATES}")
target_include_directories(mt_core PUBLIC include)
target_link_libraries(mt_core
    PUBLIC  mt::tree_sitter nlohmann_json::nlohmann_json
    PRIVATE mt::inja CLI11::CLI11)
if(MT_WITH_YAML)
    target_link_libraries(mt_core PRIVATE yaml-cpp::yaml-cpp)
endif()
# Whether the configuration reader knows YAML is a property of the library, and of whoever includes
# its headers to ask (core/config.h: yaml_supported()), not of a macro they have to define.
target_compile_definitions(mt_core PRIVATE MT_WITH_YAML=$<BOOL:${MT_WITH_YAML}>)
if(MT_WITH_LIBCLANG)
    target_link_libraries(mt_core PRIVATE mt::libclang)
endif()
target_compile_definitions(mt_core PRIVATE MT_WITH_LIBCLANG=$<BOOL:${MT_WITH_LIBCLANG}>)
set_source_files_properties(src/cli/run.cpp PROPERTIES
    COMPILE_DEFINITIONS MT_VERSION="${PROJECT_VERSION}")
mt_configure(mt_core)

# ---- command-line tool -----------------------------------------------------

add_executable(mirrortest apps/cli/main.cpp)
target_link_libraries(mirrortest PRIVATE mt_core)
mt_configure(mirrortest)

# ---- installing ------------------------------------------------------------

# The binary has everything in it (the templates are compiled in, docs/templates.md), so the install is the program, the
# licence and the documentation, laid out as in the repository so that the links between the pages work. Nothing of the
# dependencies is installed: they are linked in. A build with libclang links the system's libclang shared, and keeps the
# directory it was found in (a system one such as /usr/lib/llvm-18/lib, not the build tree) in the installed binary's
# search path, since that directory is not one the loader looks in by itself.
include(GNUInstallDirs)
if(MT_WITH_LIBCLANG)
    set_target_properties(mirrortest PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()
install(TARGETS mirrortest RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES LICENSE README.md DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY docs DESTINATION "${CMAKE_INSTALL_DOCDIR}" FILES_MATCHING PATTERN "*.md")

# ---- development tools -----------------------------------------------------

if(MT_BUILD_TOOLS)
    add_executable(mt_dump_model tools/mt_dump_model.cpp)
    target_include_directories(mt_dump_model PRIVATE tests/unit)
    target_link_libraries(mt_dump_model PRIVATE mt_core)
    mt_configure(mt_dump_model)
endif()

# ---- tests -----------------------------------------------------------------

if(MT_BUILD_TESTS)
    enable_testing()
    include(GoogleTest)

    add_executable(mt_unit_tests
        tests/unit/test_annotations.cpp
        tests/unit/test_classify.cpp
        tests/unit/test_clang_frontend.cpp
        tests/unit/test_cli.cpp
        tests/unit/test_code_frontend.cpp
        tests/unit/test_compile_commands.cpp
        tests/unit/test_config.cpp
        tests/unit/test_config_yaml.cpp
        tests/unit/test_cxx_format.cpp
        tests/unit/test_dependencies.cpp
        tests/unit/test_extract.cpp
        tests/unit/test_gen_impl.cpp
        tests/unit/test_gen_tests.cpp
        tests/unit/test_gtest_backend.cpp
        tests/unit/test_hook_header.cpp
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
        tests/unit/test_mock_macros.cpp
        tests/unit/test_mock_planner.cpp
        tests/unit/test_naming.cpp
        tests/unit/test_pipeline.cpp
        tests/unit/test_qtest_backend.cpp
        tests/unit/test_qtest_frontend.cpp
        tests/unit/test_preprocess.cpp
        tests/unit/test_regions.cpp
        tests/unit/test_source.cpp
        tests/unit/test_syntax_tree.cpp
        tests/unit/test_nested_expand.cpp
        tests/unit/test_template_expand.cpp
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
    gtest_discover_tests(mt_unit_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 120)

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
    gtest_discover_tests(mt_golden_tests TEST_PREFIX golden. DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 120)
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
    # golden case, compile the stubs as strict C++11, compile and run
    # the test, require every test red, and require that a second run changes
    # nothing. Driven by compiler command lines, not a nested CMake project, so
    # the cases are fast and need no network; GoogleTest is the main build's.
    if(NOT MSVC)
        foreach(case basic_class fixture_members namespaces multi_class_includes no_exceptions no_stl no_stl_library_types
                     stub_hook stub_hook_abort mock_interface mock_legacy_macros mock_known_interface fake_interface operators enums exceptions matchers)
            set(mode throw)
            if(case STREQUAL "no_exceptions" OR case STREQUAL "no_stl" OR case STREQUAL "no_stl_library_types" OR case STREQUAL "stub_hook_abort")
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
                    "-DMT_GTEST_INCLUDE=$<TARGET_PROPERTY:GTest::gtest,INTERFACE_INCLUDE_DIRECTORIES>$<SEMICOLON>$<TARGET_PROPERTY:GTest::gmock,INTERFACE_INCLUDE_DIRECTORIES>"
                    "-DMT_GTEST_LIBS=$<TARGET_FILE:GTest::gmock>$<SEMICOLON>$<TARGET_FILE:GTest::gtest_main>$<SEMICOLON>$<TARGET_FILE:GTest::gtest>"
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
    # relies on and the tool cannot know, and an optional <case>.mocks.cpp: one more
    # translation unit, compiled against the generated mock headers (gtest.mocks); and an optional <case>.configs, one
    # configuration of the header a line (compiler flags, or `none`), for a header that has `#if`: the case is then run in each.
    if(NOT MSVC)
        file(GLOB mt_gen_tests_impls RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/gen-tests"
             "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/gen-tests/*.cpp")
        list(FILTER mt_gen_tests_impls EXCLUDE REGEX "\\.mocks\\.cpp$")
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
            set(mocks "")
            if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/gen-tests/${case}.mocks.cpp")
                set(mocks "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/gen-tests/${case}.mocks.cpp")
            endif()
            set(configs "")
            if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/gen-tests/${case}.configs")
                set(configs "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/gen-tests/${case}.configs")
            endif()
            add_test(NAME roundtrip.gen_tests.${case}
                COMMAND "${CMAKE_COMMAND}"
                    -DMT_BIN=$<TARGET_FILE:mirrortest>
                    "-DMT_CONFIGS=${configs}"
                    -DMT_CASE_DIR=${case_dir}
                    -DMT_IMPL=${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/gen-tests/${impl}
                    "-DMT_PRELUDE=${prelude}"
                    "-DMT_MOCKS=${mocks}"
                    -DMT_WORK=${CMAKE_CURRENT_BINARY_DIR}/roundtrip/gen-tests/${case}
                    -DMT_EXPECT=${expect}
                    -DMT_CXX=${CMAKE_CXX_COMPILER}
                    "-DMT_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
                    "-DMT_GTEST_INCLUDE=$<TARGET_PROPERTY:GTest::gtest,INTERFACE_INCLUDE_DIRECTORIES>$<SEMICOLON>$<TARGET_PROPERTY:GTest::gmock,INTERFACE_INCLUDE_DIRECTORIES>"
                    "-DMT_GTEST_LIBS=$<TARGET_FILE:GTest::gmock>$<SEMICOLON>$<TARGET_FILE:GTest::gtest_main>$<SEMICOLON>$<TARGET_FILE:GTest::gtest>"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/run_gen_tests_case.cmake")
            set_tests_properties(roundtrip.gen_tests.${case} PROPERTIES TIMEOUT 300)
        endforeach()
    endif()

    # Qt Test round trips (docs/design.md 16.3), in both directions, when Qt's Test
    # module is installed: Qt 6 is looked for first, then Qt 5. They need moc, so
    # they are driven the same way as the others -- compiler command lines, moc
    # run by the script -- with Qt's include directories, libraries and moc taken
    # from its imported targets. Without Qt one test says so and is reported as
    # skipped rather than the cases silently not existing.
    set(MT_QT "")
    find_package(Qt6 QUIET COMPONENTS Test)
    if(TARGET Qt6::Test)
        set(MT_QT Qt6)
    else()
        find_package(Qt5 QUIET COMPONENTS Test)
        if(TARGET Qt5::Test)
            set(MT_QT Qt5)
        endif()
    endif()
    if(MT_QT AND NOT MSVC)
        message(STATUS "Qt Test round trips: ${MT_QT} ${${MT_QT}_VERSION}")
        set(mt_qt_args
            "-DMT_QT_INCLUDE=$<TARGET_PROPERTY:${MT_QT}::Test,INTERFACE_INCLUDE_DIRECTORIES>$<SEMICOLON>$<TARGET_PROPERTY:${MT_QT}::Core,INTERFACE_INCLUDE_DIRECTORIES>"
            "-DMT_QT_LIBS=$<TARGET_FILE:${MT_QT}::Test>$<SEMICOLON>$<TARGET_FILE:${MT_QT}::Core>"
            -DMT_MOC=$<TARGET_FILE:${MT_QT}::moc>
            -DMT_BIN=$<TARGET_FILE:mirrortest>
            -DMT_CXX=${CMAKE_CXX_COMPILER}
            "-DMT_CXX_FLAGS=${CMAKE_CXX_FLAGS}")

        # gen-tests: the golden cases that have an example implementation in tests/roundtrip/qt/.
        file(GLOB mt_qt_impls RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/qt"
             "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/qt/*.cpp")
        list(SORT mt_qt_impls)
        foreach(impl IN LISTS mt_qt_impls)
            string(REGEX REPLACE "\\.cpp$" "" case "${impl}")
            set(case_dir "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen-tests/${case}")
            file(READ "${case_dir}/args.txt" case_args)
            set(expect fail)
            if(case_args MATCHES "\n--placeholder\nskip\n")
                set(expect skip)
            endif()
            set(prelude "")
            if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/qt/${case}.prelude.h")
                set(prelude "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/qt/${case}.prelude.h")
            endif()
            add_test(NAME roundtrip.qt.gen_tests.${case}
                COMMAND "${CMAKE_COMMAND}" ${mt_qt_args}
                    -DMT_DIRECTION=gen-tests
                    -DMT_CASE_DIR=${case_dir}
                    -DMT_IMPL=${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/qt/${impl}
                    "-DMT_PRELUDE=${prelude}"
                    -DMT_EXPECT=${expect}
                    -DMT_WORK=${CMAKE_CURRENT_BINARY_DIR}/roundtrip/qt/gen-tests/${case}
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/run_qt_case.cmake")
            set_tests_properties(roundtrip.qt.gen_tests.${case} PROPERTIES TIMEOUT 300)
        endforeach()

        # gen-impl: the golden cases whose test runs. qt_exceptions_new needs the macros of Qt 6.3.
        set(mt_qt_impl_cases qt_basic qt_inline_slots qt_exceptions qt_hook)
        if(MT_QT STREQUAL "Qt6" AND Qt6_VERSION VERSION_GREATER_EQUAL 6.3)
            list(APPEND mt_qt_impl_cases qt_exceptions_new)
        endif()
        foreach(case IN LISTS mt_qt_impl_cases)
            # stub_policy hook: a handler that fails the slot and returns, so that all slots run.
            set(handler "")
            if(case STREQUAL "qt_hook")
                set(handler "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/hook/qt_handler.cpp")
            endif()
            add_test(NAME roundtrip.qt.gen_impl.${case}
                COMMAND "${CMAKE_COMMAND}" ${mt_qt_args}
                    -DMT_DIRECTION=gen-impl
                    -DMT_CASE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen-impl/${case}
                    "-DMT_HANDLER=${handler}"
                    -DMT_WORK=${CMAKE_CURRENT_BINARY_DIR}/roundtrip/qt/gen-impl/${case}
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/run_qt_case.cmake")
            set_tests_properties(roundtrip.qt.gen_impl.${case} PROPERTIES TIMEOUT 300)
        endforeach()
    elseif(NOT MSVC)
        message(STATUS "Qt Test round trips: skipped, no Qt 6 or Qt 5 Test module found")
        add_test(NAME roundtrip.qt COMMAND "${CMAKE_COMMAND}" -E echo "MT_SKIPPED: the Qt Test round trips need Qt 6 or Qt 5 with the Test module")
        set_tests_properties(roundtrip.qt PROPERTIES SKIP_REGULAR_EXPRESSION "MT_SKIPPED")
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
    # host compiler always, and with MT_CROSS_CXX (a cross compiler for a 32-bit
    # target, e.g. arm-linux-gnueabihf-g++, which CI installs) to see the stubs
    # where `long` and `size_t` are narrower. Syntax only: nothing is linked or run.
    set(MT_CROSS_CXX "" CACHE STRING "Cross compiler for the stub syntax checks (empty: skip them)")
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
        # A case whose headers include a Qt class needs Qt to be checked at all, and Qt is a host
        # library: it is checked where Qt is installed, never with the cross compiler.
        set(case_qt FALSE)
        file(GLOB_RECURSE case_headers "${case_dir}/expected/*.h")
        foreach(header IN LISTS case_headers)
            file(READ "${header}" header_text)
            if(header_text MATCHES "#include <Q")
                set(case_qt TRUE)
            endif()
        endforeach()
        if(case_qt AND NOT MT_QT)
            continue()
        endif()
        foreach(kind host cross)
            if(case_qt AND kind STREQUAL "cross")
                continue()
            endif()
            if(kind STREQUAL "host")
                set(compiler "${CMAKE_CXX_COMPILER}")
            else()
                set(compiler "${MT_CROSS_CXX}")
            endif()
            if(compiler)
                add_test(NAME stubs.${kind}.${case}
                    COMMAND "${CMAKE_COMMAND}"
                        -DMT_BIN=$<TARGET_FILE:mirrortest>
                        -DMT_CASE_DIR=${case_dir}
                        -DMT_WORK=${CMAKE_CURRENT_BINARY_DIR}/stubs-${kind}/${case}
                        -DMT_CXX=${compiler}
                        "$<$<BOOL:${case_qt}>:-DMT_QT_INCLUDE=$<TARGET_PROPERTY:${MT_QT}::Test,INTERFACE_INCLUDE_DIRECTORIES>$<SEMICOLON>$<TARGET_PROPERTY:${MT_QT}::Core,INTERFACE_INCLUDE_DIRECTORIES>>"
                        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip/syntax_case.cmake")
                set_tests_properties(stubs.${kind}.${case} PROPERTIES TIMEOUT 120)
            endif()
        endforeach()
    endforeach()

    # The CLI exists and answers. Behavioural CLI coverage lives in the golden
    # and round-trip suites, which drive the real binary.
    add_test(NAME cli.version COMMAND mirrortest --version)
    set_tests_properties(cli.version PROPERTIES PASS_REGULAR_EXPRESSION "mirrortest ${PROJECT_VERSION}")

    # The script that runs the tool over code nobody wrote for it (docs/development.md) runs, and finds nothing wrong
    # in the headers of the golden cases: its exit status is non-zero when a crash, a hang or a sanitizer report is.
    if(MT_BUILD_TOOLS AND MT_WITH_LIBCLANG AND UNIX)
        add_test(NAME tools.corpus
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/corpus.sh"
                --bin $<TARGET_FILE:mirrortest> --dump $<TARGET_FILE:mt_dump_model> --jobs 2
                --out "${CMAKE_CURRENT_BINARY_DIR}/corpus-smoke"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen-tests/abstract/input")
    endif()
endif()

# ---- packaging -------------------------------------------------------------

# `cpack -G TGZ` (or `cmake --build build --target package`) makes mirrortest-<version>-<system>-<machine>.tar.gz from the
# install above. Publishing it is the owner's decision, not the build's.
set(CPACK_GENERATOR "TGZ")
set(CPACK_PACKAGE_NAME "mirrortest")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Bidirectional C++ test/code generator")
set(CPACK_PACKAGE_FILE_NAME "mirrortest-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
include(CPack)
