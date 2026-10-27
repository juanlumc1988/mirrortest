# SPDX-License-Identifier: Apache-2.0
# Part of mirrortest -- bidirectional C++ test/code generator.
#
# Third-party dependencies, fetched at configure time and pinned twice: by tag
# (so the clone can be shallow and fast) and by commit (so a tag that moved
# upstream fails the configure instead of silently changing the build).
#
# Why git and not release tarballs: GitHub's /archive/ tarballs are not
# byte-stable across their history and are blocked outright by some egress
# proxies, while a shallow git clone of a tag works anywhere git does.
#
# Offline or air-gapped builds: point FetchContent at local copies, e.g.
#   -DFETCHCONTENT_SOURCE_DIR_TREE_SITTER=/mirror/tree-sitter
# A git checkout given that way is still verified against the pinned commit;
# a plain directory is used as-is and the configure log says so.
# -DMT_SKIP_DEPENDENCY_VERIFY=ON disables the check entirely -- for trying a
# newer version on purpose, never for a release build.
#
# Licences, all compatible with Apache-2.0:
#   tree-sitter, tree-sitter-cpp, nlohmann/json, inja   MIT
#   yaml-cpp (only with MT_WITH_YAML)                    MIT
#   CLI11, GoogleTest                                   BSD-3-Clause

include(FetchContent)

option(MT_SKIP_DEPENDENCY_VERIFY "Do not check fetched dependencies against their pinned commits" OFF)

find_package(Git REQUIRED)

# mt_declare(<name> <repository> <tag> <commit> [extra FetchContent args...])
function(mt_declare name repository tag commit)
    FetchContent_Declare(${name}
        GIT_REPOSITORY ${repository}
        GIT_TAG        ${tag}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   FALSE
        ${ARGN})
    set_property(GLOBAL PROPERTY MT_PINNED_COMMIT_${name} ${commit})
    set_property(GLOBAL PROPERTY MT_PINNED_TAG_${name} ${tag})
endfunction()

# Must run after FetchContent_MakeAvailable(<name>).
function(mt_verify name)
    if(MT_SKIP_DEPENDENCY_VERIFY)
        return()
    endif()
    get_property(expected GLOBAL PROPERTY MT_PINNED_COMMIT_${name})
    get_property(tag GLOBAL PROPERTY MT_PINNED_TAG_${name})
    string(TOLOWER ${name} lname)
    set(dir "${${lname}_SOURCE_DIR}")
    if(NOT EXISTS "${dir}/.git")
        message(STATUS "mirrortest: ${name} taken from ${dir} as-is (not a git checkout, commit not verified)")
        return()
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${dir}"
        OUTPUT_VARIABLE actual
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0 OR NOT actual STREQUAL expected)
        message(FATAL_ERROR
            "mirrortest: ${name} at ${dir} is commit '${actual}', but ${tag} is pinned to ${expected}.\n"
            "Either the upstream tag moved or a different checkout was supplied. "
            "Re-pin deliberately in cmake/MtDependencies.cmake, or pass "
            "-DMT_SKIP_DEPENDENCY_VERIFY=ON to accept it for this build tree.")
    endif()
endfunction()

# CMake < 3.25 has no SYSTEM keyword for FetchContent. Re-export a target's
# include directories as system ones so -Werror on our code never trips over
# a warning in somebody else's header.
function(mt_as_system target)
    get_target_property(aliased ${target} ALIASED_TARGET)
    if(aliased)
        set(target ${aliased})
    endif()
    get_target_property(dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
    if(dirs)
        set_target_properties(${target} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${dirs}")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Pins. Changing one is a reviewed change: bump tag and commit together and
# rerun the golden and round-trip tests.
# ---------------------------------------------------------------------------

# Runtime ABI 13..15; the C++ grammar below is ABI 14.
mt_declare(tree_sitter
    https://github.com/tree-sitter/tree-sitter.git
    v0.25.10 da6fe9beb4f7f67beb75914ca8e0d48ae48d6406
    SOURCE_SUBDIR _mt_no_cmake)   # we build lib.c ourselves; see below

mt_declare(tree_sitter_cpp
    https://github.com/tree-sitter/tree-sitter-cpp.git
    v0.23.4 f41e1a044c8a84ea9fa8577fdd2eab92ec96de02
    SOURCE_SUBDIR _mt_no_cmake)

mt_declare(nlohmann_json
    https://github.com/nlohmann/json.git
    v3.12.0 55f93686c01528224f448c19128836e7df245f72)

mt_declare(inja
    https://github.com/pantor/inja.git
    v3.5.0 7d1b4600b68595085a949743331c2e5673f511ea
    SOURCE_SUBDIR _mt_no_cmake)   # header-only; its CMakeLists pulls tests

mt_declare(cli11
    https://github.com/CLIUtils/CLI11.git
    v2.5.0 4160d259d961cd393fd8d67590a8c7d210207348)

# 1.16.x is the newest line that still accepts C++14 (1.17 requires C++17).
# The round-trip tests compile generated tests with -std=c++14, so do not
# bump past 1.16 without dropping that coverage on purpose.
mt_declare(googletest
    https://github.com/google/googletest.git
    v1.16.0 6910c9d9165801d8827d628cb72eb7ea9dd538c5)

# Only needed to read a configuration written in YAML (docs/design.md 14); the JSON reader stays
# whether it is built or not.
mt_declare(yaml_cpp
    https://github.com/jbeder/yaml-cpp.git
    yaml-cpp-0.9.0 56e3bb550c91fd7005566f19c079cb7a503223cf)

# ---- tree-sitter runtime + C++ grammar ------------------------------------

FetchContent_MakeAvailable(tree_sitter tree_sitter_cpp)
mt_verify(tree_sitter)
mt_verify(tree_sitter_cpp)

# Third-party C: built with its own upstream flags, warnings silenced.
add_library(mt_tree_sitter STATIC
    "${tree_sitter_SOURCE_DIR}/lib/src/lib.c"
    "${tree_sitter_cpp_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c")
target_include_directories(mt_tree_sitter
    PRIVATE
        "${tree_sitter_SOURCE_DIR}/lib/src"
        "${tree_sitter_cpp_SOURCE_DIR}/src")
target_include_directories(mt_tree_sitter SYSTEM
    PUBLIC
        "${tree_sitter_SOURCE_DIR}/lib/include")
target_compile_definitions(mt_tree_sitter
    PRIVATE _POSIX_C_SOURCE=200112L _DEFAULT_SOURCE)
set_target_properties(mt_tree_sitter PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mt_tree_sitter PRIVATE -w)
endif()
add_library(mt::tree_sitter ALIAS mt_tree_sitter)

# ---- nlohmann/json + inja --------------------------------------------------

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)
mt_verify(nlohmann_json)
mt_as_system(nlohmann_json)

FetchContent_MakeAvailable(inja)
mt_verify(inja)
add_library(mt_inja INTERFACE)
target_include_directories(mt_inja SYSTEM INTERFACE "${inja_SOURCE_DIR}/include")
target_link_libraries(mt_inja INTERFACE nlohmann_json::nlohmann_json)
add_library(mt::inja ALIAS mt_inja)

# ---- yaml-cpp (optional) ---------------------------------------------------

if(MT_WITH_YAML)
    set(YAML_CPP_BUILD_TESTS   OFF CACHE INTERNAL "")
    set(YAML_CPP_BUILD_TOOLS   OFF CACHE INTERNAL "")
    set(YAML_CPP_BUILD_CONTRIB OFF CACHE INTERNAL "")
    set(YAML_CPP_INSTALL       OFF CACHE INTERNAL "")
    set(YAML_CPP_FORMAT_SOURCE OFF CACHE INTERNAL "")
    set(YAML_BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(yaml_cpp)
    mt_verify(yaml_cpp)
    mt_as_system(yaml-cpp)
endif()

# ---- CLI11 -----------------------------------------------------------------

FetchContent_MakeAvailable(cli11)
mt_verify(cli11)
mt_as_system(CLI11)

# ---- GoogleTest (tests only) ----------------------------------------------

if(MT_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE INTERNAL "")
    set(BUILD_GMOCK   ON  CACHE INTERNAL "")
    set(gtest_force_shared_crt ON CACHE INTERNAL "")
    FetchContent_MakeAvailable(googletest)
    mt_verify(googletest)
    foreach(t gtest gtest_main gmock gmock_main)
        mt_as_system(${t})
    endforeach()
endif()
