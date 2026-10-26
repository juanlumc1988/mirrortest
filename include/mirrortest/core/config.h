// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Everything the user can tune. docs/design.md, section 14, is the reference
// for each key.
//
// Precedence (design D9): built-in defaults, then --config FILE, then the
// command line. That is implemented as three layers applied in turn to one
// Config: a default-constructed Config is layer one, apply_config_json() is
// layer two, and the CLI sets the fields of the options it was given on top.
// There is no implicit discovery of a configuration file.
//
// Validation is strict because the alternative is silent: a misspelt key that
// is ignored means a type_map that never applies, and nobody notices until
// the generated code is wrong. Unknown keys, wrong types and values outside
// their set are all `mt-config` errors, and all of them are reported in one
// pass. A file with any error leaves the Config untouched.

#ifndef MT_CORE_CONFIG_H
#define MT_CORE_CONFIG_H

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mirrortest/core/diagnostics.h"

namespace mt::core {

enum class CxxStd { Cxx11, Cxx14, Cxx17, Cxx20, Cxx23 };
enum class GuardStyle { Ifndef, PragmaOnce };
enum class StubPolicy { Throw, Abort, Default };
enum class Placeholder { Fail, Skip };
enum class AutoPolicy { Error, Todo };
// How a generated Qt Test file starts its event loop: QTEST_APPLESS_MAIN (no
// application object), QTEST_GUILESS_MAIN (a QCoreApplication) or QTEST_MAIN
// (a QApplication).
enum class QtMain { Appless, GuiLess, Gui };

// The spellings used on the command line and in the configuration file.
const char* to_string(CxxStd value);          // "c++11" .. "c++23"
const char* to_string(GuardStyle value);      // "ifndef", "pragma-once"
const char* to_string(StubPolicy value);      // "throw", "abort", "default"
const char* to_string(Placeholder value);     // "fail", "skip"
const char* to_string(AutoPolicy value);      // "error", "todo"

std::optional<CxxStd>     parse_std(std::string_view text);
std::optional<GuardStyle> parse_guard_style(std::string_view text);
std::optional<StubPolicy> parse_stub_policy(std::string_view text);
std::optional<Placeholder> parse_placeholder(std::string_view text);
std::optional<AutoPolicy> parse_auto_policy(std::string_view text);

// The valid spellings of each enumeration, in declaration order. The CLI
// validates its options against these and prints them in --help, so the
// command line and the configuration file can never disagree.
std::vector<std::string> std_names();
std::vector<std::string> guard_names();
std::vector<std::string> stub_policy_names();
std::vector<std::string> placeholder_names();
std::vector<std::string> auto_policy_names();

// 11, 14, 17, 20, 23.
int to_number(CxxStd value);

// File name and test-name patterns. {stem} is the snake_case class name,
// {Class} the class name as written.
struct Naming {
    std::string header      = "{stem}.h";
    std::string source      = "{stem}.cpp";
    std::string gtest_file  = "{stem}_test.cpp";
    std::string qtest_file  = "tst_{stem}.cpp";
    std::string suite       = "{Class}Test";
    std::string qtest_class = "Test{Class}";
    std::string mock_file   = "mock_{stem}.h";    // the gmock mock of an interface (gtest.mocks)
    std::string mock_class  = "Mock{Class}";
};

// What a configuration file may say about one member, with the same meaning
// as the long form of a "// @mt:" annotation (design 7.11). Unset = no
// statement. Types are stored normalised.
struct MemberOverride {
    std::optional<std::string>      returns;
    std::map<unsigned, std::string> params;        // 1-based position -> type
    std::optional<bool>             is_const;
    std::optional<bool>             is_static;
    std::optional<bool>             is_virtual;
    std::optional<bool>             is_noexcept;
};

// What a configuration file may say about the GoogleTest skeletons of gen-tests.
struct GtestConfig {
    // TEST_F with a fixture class that owns the object under test, instead of a TEST
    // that builds it. The fixture builds the object *before* the test body, so unlike
    // the plain skeleton it runs the class's constructor even while the placeholder
    // keeps the test red: that is why it is opt-in (design 9.4).
    bool fixture = false;

    // A gmock mock, in a header of its own, for every interface (a class with pure
    // virtual functions) the header declares. Opt-in because it writes files nobody
    // asked for (design 9.4).
    bool mocks = false;

    // A method that takes arguments is a TEST_P over a struct of them with one row of
    // default values, to be given the rows that matter (design 9.4). Opt-in because it
    // changes the shape of the file, and it needs GoogleTest 1.10 or later.
    bool parameterised = false;
};

// What a configuration file may say about the Qt Test skeletons of gen-tests.
struct QtestConfig {
    // A method that takes arguments gets a `<name>_data()` slot with one column for each
    // argument and one row of default values, and the test reads them with QFETCH (design 9.5).
    bool data_driven = false;
};

struct Config {
    CxxStd      cxx_std           = CxxStd::Cxx17;
    std::string default_namespace;                  // "" = global, else "a::b"
    Naming      naming;
    GtestConfig gtest;
    QtestConfig qtest;
    GuardStyle  guard             = GuardStyle::Ifndef;
    std::string file_header;                        // comment text, "" = none

    StubPolicy  stub_policy       = StubPolicy::Throw;
    bool        no_exceptions     = false;          // see effective_stub_policy()
    bool        no_rtti           = false;
    Placeholder placeholder       = Placeholder::Fail;
    AutoPolicy  auto_policy       = AutoPolicy::Error;
    std::string placeholder_type  = "int";          // type for what is unknown under auto_policy=todo
    std::string string_literal_param = "const char*";
    int         qt                = 6;              // 5 or 6

    // gen-impl: a type that only appears as the exception of EXPECT_THROW(stmt, E) is not a class
    // the tool can generate by itself, for it needs a base the test does not state. With this set
    // ("std::runtime_error") it is generated, deriving from it. Empty (the default): it must come
    // from a header, or be reported.
    std::string exception_base;

    // Applied last, and only to what was inferred (never to what the user
    // declared or annotated). Sorted by key: lookups only, never emission
    // order, so determinism does not depend on it.
    std::map<std::string, std::string> type_map;

    std::vector<std::string> by_value_types;
    std::vector<std::string> default_constructible_types;
    std::vector<std::string> external_types;
    std::vector<std::string> external_functions;
    std::vector<std::string> ignore_macros;

    std::map<std::string, MemberOverride>             overrides;               // "ns::Class::member"
    std::map<std::string, std::vector<std::string>>  template_instantiations;  // "ns::Box" -> {"int", ...}
};

// Without exceptions a stub cannot throw, so `throw` becomes `abort`. An
// explicit `default` is kept: it never throws either.
StubPolicy effective_stub_policy(const Config& config);

// Applies the keys present in `json` on top of `config`. Comments (// and
// /* */) are allowed in the JSON. `origin` names the source in diagnostics.
//
// Returns true on success. On failure every problem has been recorded as an
// `mt-config` error and `config` is exactly as it was.
bool apply_config_json(Config& config, std::string_view json, const std::string& origin,
                       Diagnostics& diagnostics);

// Reads `path` (an `mt-io` error if it cannot be read) and applies it. A
// relative path is resolved against `base_dir` if given; diagnostics show it
// as written.
bool load_config_file(Config& config, const std::string& path, Diagnostics& diagnostics,
                      const std::filesystem::path& base_dir = {});

}  // namespace mt::core

#endif  // MT_CORE_CONFIG_H
