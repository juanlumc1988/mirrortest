// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/core/config.h"
#include "test_support.h"

using namespace mt::core;

namespace {

struct Applied {
    bool        ok;
    Config      config;
    Diagnostics diagnostics;
};

Applied apply(const std::string& json, Config start = {}) {
    Applied a{false, std::move(start), {}};
    a.ok = apply_config_json(a.config, json, "mt.json", a.diagnostics);
    return a;
}

// The single error a config produced, or "" if there was none / several.
std::string only_error(const Applied& a) {
    if (a.diagnostics.all().size() != 1) {
        return "";
    }
    EXPECT_EQ(a.diagnostics.all()[0].code, code::config);
    EXPECT_EQ(a.diagnostics.all()[0].where.file, "mt.json");
    return a.diagnostics.all()[0].message;
}

}  // namespace

// ---- defaults ---------------------------------------------------------------------

TEST(ConfigDefaults, MatchTheDesignDocument) {
    const Config c;
    EXPECT_EQ(c.cxx_std, CxxStd::Cxx17);
    EXPECT_EQ(c.default_namespace, "");
    EXPECT_EQ(c.naming.header, "{stem}.h");
    EXPECT_EQ(c.naming.source, "{stem}.cpp");
    EXPECT_EQ(c.naming.gtest_file, "{stem}_test.cpp");
    EXPECT_EQ(c.naming.qtest_file, "tst_{stem}.cpp");
    EXPECT_EQ(c.naming.suite, "{Class}Test");
    EXPECT_EQ(c.naming.qtest_class, "Test{Class}");
    EXPECT_EQ(c.naming.mock_file, "mock_{stem}.h");
    EXPECT_EQ(c.naming.mock_class, "Mock{Class}");
    EXPECT_EQ(c.guard, GuardStyle::Ifndef);
    EXPECT_EQ(c.file_header, "");
    EXPECT_EQ(c.stub_policy, StubPolicy::Throw);
    EXPECT_FALSE(c.no_exceptions);
    EXPECT_FALSE(c.no_stl);
    EXPECT_FALSE(c.no_rtti);
    EXPECT_EQ(c.placeholder, Placeholder::Fail);
    EXPECT_EQ(c.auto_policy, AutoPolicy::Error);
    EXPECT_EQ(c.placeholder_type, "int");
    EXPECT_EQ(c.string_literal_param, "const char*");
    EXPECT_EQ(c.exception_base, "") << "no exception class is generated unless asked";
    EXPECT_EQ(c.qt, 6);
    EXPECT_FALSE(c.gtest.fixture);
    EXPECT_FALSE(c.gtest.mocks);
    EXPECT_FALSE(c.gtest.parameterised);
    EXPECT_FALSE(c.qtest.data_driven);
    EXPECT_TRUE(c.type_map.empty());
    EXPECT_TRUE(c.by_value_types.empty());
    EXPECT_TRUE(c.overrides.empty());
    EXPECT_TRUE(c.template_instantiations.empty());
}

TEST(ConfigDefaults, AnEmptyObjectChangesNothing) {
    const Applied a = apply("{}");
    EXPECT_TRUE(a.ok);
    EXPECT_TRUE(a.diagnostics.empty());
    EXPECT_EQ(a.config.cxx_std, CxxStd::Cxx17);
}

// ---- every key ----------------------------------------------------------------------

TEST(ConfigKeys, EveryKeyIsRead) {
    const Applied a = apply(R"({
        "std": "c++11",
        "default_namespace": "acme::core",
        "naming": { "header": "include/{stem}.h", "source": "src/{stem}.cpp",
                    "gtest_file": "{stem}_tests.cpp", "qtest_file": "qt_{stem}.cpp",
                    "suite": "Test{Class}", "qtest_class": "{Class}Spec",
                    "mock_file": "mocks/{stem}_mock.h", "mock_class": "{Class}Mock" },
        "gtest": { "fixture": true, "mocks": true, "parameterised": true },
        "qtest": { "data_driven": true },
        "guard": "pragma-once",
        "file_header": "// Copyright ACME\n// All rights reserved\n",
        "stub_policy": "default",
        "no_exceptions": true,
        "no_stl": true,
        "no_rtti": true,
        "placeholder": "skip",
        "auto_policy": "todo",
        "placeholder_type": "long",
        "string_literal_param": "std::string",
        "qt": 5,
        "type_map": { "unsigned int": "std::uint32_t" },
        "by_value_types": ["Handle"],
        "default_constructible_types": ["Handle"],
        "external_types": ["Hal", "board::Pin"],
        "external_functions": ["delay_ms", "board::reset"],
        "ignore_macros": ["MY_API", "MY_ATTR"],
        "overrides": { "acme::Foo::items": { "returns": "std::vector<int>" } },
        "template_instantiations": { "acme::Box": ["int", "std::string"] }
    })");
    ASSERT_TRUE(a.ok) << a.diagnostics.all()[0].message;
    EXPECT_TRUE(a.diagnostics.empty());

    const Config& c = a.config;
    EXPECT_EQ(c.cxx_std, CxxStd::Cxx11);
    EXPECT_EQ(c.default_namespace, "acme::core");
    EXPECT_EQ(c.naming.header, "include/{stem}.h");
    EXPECT_EQ(c.naming.source, "src/{stem}.cpp");
    EXPECT_EQ(c.naming.gtest_file, "{stem}_tests.cpp");
    EXPECT_EQ(c.naming.qtest_file, "qt_{stem}.cpp");
    EXPECT_EQ(c.naming.suite, "Test{Class}");
    EXPECT_EQ(c.naming.qtest_class, "{Class}Spec");
    EXPECT_EQ(c.naming.mock_file, "mocks/{stem}_mock.h");
    EXPECT_EQ(c.naming.mock_class, "{Class}Mock");
    EXPECT_TRUE(c.gtest.fixture);
    EXPECT_TRUE(c.gtest.mocks);
    EXPECT_TRUE(c.gtest.parameterised);
    EXPECT_TRUE(c.qtest.data_driven);
    EXPECT_EQ(c.guard, GuardStyle::PragmaOnce);
    EXPECT_EQ(c.file_header, "// Copyright ACME\n// All rights reserved");
    EXPECT_EQ(c.stub_policy, StubPolicy::Default);
    EXPECT_TRUE(c.no_exceptions);
    EXPECT_TRUE(c.no_stl);
    EXPECT_TRUE(c.no_rtti);
    EXPECT_EQ(c.placeholder, Placeholder::Skip);
    EXPECT_EQ(c.auto_policy, AutoPolicy::Todo);
    EXPECT_EQ(c.placeholder_type, "long");
    EXPECT_EQ(c.string_literal_param, "std::string");
    EXPECT_EQ(c.qt, 5);
    EXPECT_EQ(c.type_map.at("unsigned int"), "std::uint32_t");
    EXPECT_EQ(c.by_value_types, std::vector<std::string>{"Handle"});
    EXPECT_EQ(c.default_constructible_types, std::vector<std::string>{"Handle"});
    EXPECT_EQ(c.external_types, (std::vector<std::string>{"Hal", "board::Pin"}));
    EXPECT_EQ(c.external_functions, (std::vector<std::string>{"delay_ms", "board::reset"}));
    EXPECT_EQ(c.ignore_macros, (std::vector<std::string>{"MY_API", "MY_ATTR"}));
    EXPECT_EQ(c.overrides.at("acme::Foo::items").returns, std::optional<std::string>("std::vector<int>"));
    EXPECT_EQ(c.template_instantiations.at("acme::Box"), (std::vector<std::string>{"int", "std::string"}));
}

TEST(ConfigKeys, EveryEnumerationAcceptsEveryDocumentedValue) {
    for (const char* v : {"c++11", "c++14", "c++17", "c++20", "c++23"}) {
        EXPECT_TRUE(apply(std::string(R"({"std":")") + v + "\"}").ok) << v;
    }
    for (const char* v : {"ifndef", "pragma-once"}) {
        EXPECT_TRUE(apply(std::string(R"({"guard":")") + v + "\"}").ok) << v;
    }
    for (const char* v : {"throw", "abort", "default"}) {
        EXPECT_TRUE(apply(std::string(R"({"stub_policy":")") + v + "\"}").ok) << v;
    }
    for (const char* v : {"fail", "skip"}) {
        EXPECT_TRUE(apply(std::string(R"({"placeholder":")") + v + "\"}").ok) << v;
    }
    for (const char* v : {"error", "todo"}) {
        EXPECT_TRUE(apply(std::string(R"({"auto_policy":")") + v + "\"}").ok) << v;
    }
    EXPECT_TRUE(apply(R"({"qt":5})").ok);
    EXPECT_TRUE(apply(R"({"qt":6})").ok);
}

TEST(ConfigKeys, OnlyTheKeysPresentChange) {
    const Applied a = apply(R"({"std":"c++14"})");
    ASSERT_TRUE(a.ok);
    EXPECT_EQ(a.config.cxx_std, CxxStd::Cxx14);
    EXPECT_EQ(a.config.guard, GuardStyle::Ifndef);
    EXPECT_EQ(a.config.naming.header, "{stem}.h");
}

TEST(ConfigKeys, PartialNamingKeepsTheOtherPatterns) {
    const Applied a = apply(R"({"naming":{"suite":"Test{Class}"}})");
    ASSERT_TRUE(a.ok);
    EXPECT_EQ(a.config.naming.suite, "Test{Class}");
    EXPECT_EQ(a.config.naming.header, "{stem}.h");
    EXPECT_EQ(a.config.naming.qtest_class, "Test{Class}");
}

TEST(ConfigKeys, CommentsAreAllowedInTheFile) {
    const Applied a = apply("{\n  // project policy\n  \"std\": \"c++11\", /* the project's standard */\n  \"no_exceptions\": true\n}\n");
    ASSERT_TRUE(a.ok);
    EXPECT_EQ(a.config.cxx_std, CxxStd::Cxx11);
    EXPECT_TRUE(a.config.no_exceptions);
}

TEST(ConfigKeys, TypesAreStoredNormalised) {
    const Applied a = apply(R"({
        "type_map": { "unsigned   int": "std :: uint32_t", "const  char *": "const char*" },
        "by_value_types": ["std::array< int,3 >"],
        "placeholder_type": "unsigned   long",
        "overrides": { "A::f": { "returns": "std::vector< int >&", "params": { "2": "const  Foo &" } } },
        "template_instantiations": { "Box": ["std::map< int,int >"] }
    })");
    ASSERT_TRUE(a.ok) << a.diagnostics.all()[0].message;
    EXPECT_EQ(a.config.type_map.at("unsigned int"), "std::uint32_t");
    EXPECT_EQ(a.config.type_map.at("const char*"), "const char*");
    EXPECT_EQ(a.config.by_value_types[0], "std::array<int, 3>");
    EXPECT_EQ(a.config.placeholder_type, "unsigned long");
    EXPECT_EQ(a.config.overrides.at("A::f").returns, std::optional<std::string>("std::vector<int>&"));
    EXPECT_EQ(a.config.overrides.at("A::f").params.at(2), "const Foo&");
    EXPECT_EQ(a.config.template_instantiations.at("Box")[0], "std::map<int, int>");
}

TEST(ConfigKeys, ExceptionBaseIsAType) {
    EXPECT_EQ(apply(R"({"exception_base": "std :: runtime_error"})").config.exception_base, "std::runtime_error");
    Config start;
    start.exception_base = "std::exception";
    EXPECT_EQ(apply(R"({"exception_base": ""})", start).config.exception_base, "") << "empty says the default aloud";
    EXPECT_EQ(apply(R"({})", start).config.exception_base, "std::exception") << "and absent leaves it as it was";
}

TEST(ConfigKeys, AllMemberOverrideFields) {
    const Applied a = apply(R"({"overrides": {"ns::Foo::run": {
        "returns": "bool", "params": {"1": "int", "3": "double"},
        "const": true, "static": false, "virtual": true, "noexcept": true}}})");
    ASSERT_TRUE(a.ok);
    const MemberOverride& m = a.config.overrides.at("ns::Foo::run");
    EXPECT_EQ(m.returns, std::optional<std::string>("bool"));
    EXPECT_EQ(m.params.size(), 2u);
    EXPECT_EQ(m.params.at(1), "int");
    EXPECT_EQ(m.params.at(3), "double");
    EXPECT_EQ(m.is_const, std::optional<bool>(true));
    EXPECT_EQ(m.is_static, std::optional<bool>(false));
    EXPECT_EQ(m.is_virtual, std::optional<bool>(true));
    EXPECT_EQ(m.is_noexcept, std::optional<bool>(true));
}

TEST(ConfigKeys, FileHeaderIsNormalised) {
    EXPECT_EQ(apply(R"({"file_header":"/* a */\r\n/* b */\r\n\r\n"})").config.file_header, "/* a */\n/* b */");
    EXPECT_EQ(apply(R"({"file_header":"   \n"})").config.file_header, "");
    EXPECT_EQ(apply(R"({"file_header":""})").config.file_header, "");
}

// ---- layering ------------------------------------------------------------------------

TEST(ConfigLayers, AFileOnTopOfAFileOverridesOnlyWhatItSays) {
    Applied first = apply(R"({"std":"c++11","guard":"pragma-once","by_value_types":["A"]})");
    ASSERT_TRUE(first.ok);
    const Applied second = apply(R"({"std":"c++14","by_value_types":["B"]})", first.config);
    ASSERT_TRUE(second.ok);
    EXPECT_EQ(second.config.cxx_std, CxxStd::Cxx14);
    EXPECT_EQ(second.config.guard, GuardStyle::PragmaOnce);
    EXPECT_EQ(second.config.by_value_types, std::vector<std::string>{"B"}) << "lists are replaced, not merged";
}

// ---- errors ----------------------------------------------------------------------------

TEST(ConfigErrors, InvalidJsonIsOneLocatedError) {
    const Applied a = apply("{ \"std\": ");
    EXPECT_FALSE(a.ok);
    ASSERT_EQ(a.diagnostics.all().size(), 1u);
    EXPECT_EQ(a.diagnostics.all()[0].message.rfind("invalid JSON: ", 0), 0u);
    EXPECT_EQ(a.diagnostics.all()[0].code, code::config);
}

TEST(ConfigErrors, EmptyInputIsInvalidJson) {
    EXPECT_FALSE(apply("").ok);
}

TEST(ConfigErrors, TheRootMustBeAnObject) {
    EXPECT_EQ(only_error(apply("[]")), "the configuration must be a JSON object");
    EXPECT_EQ(only_error(apply("\"std\"")), "the configuration must be a JSON object");
}

TEST(ConfigErrors, UnknownKeysAreNamedWithTheirPath) {
    EXPECT_EQ(only_error(apply(R"({"colour":"red"})")), "unknown configuration key 'colour'");
    EXPECT_EQ(only_error(apply(R"({"naming":{"colour":"red"}})")),
              "unknown configuration key 'naming.colour'");
    EXPECT_EQ(only_error(apply(R"({"gtest":{"fixtur":true}})")), "unknown configuration key 'gtest.fixtur'");
    EXPECT_EQ(only_error(apply(R"({"gtest":{"fixture":1}})")), "configuration key 'gtest.fixture' must be true or false");
    EXPECT_EQ(only_error(apply(R"({"gtest":{"mocks":"yes"}})")), "configuration key 'gtest.mocks' must be true or false");
    EXPECT_EQ(only_error(apply(R"({"gtest":{"parameterised":1}})")), "configuration key 'gtest.parameterised' must be true or false");
    EXPECT_EQ(only_error(apply(R"({"qtest":{"data_driven":"yes"}})")), "configuration key 'qtest.data_driven' must be true or false");
    EXPECT_EQ(only_error(apply(R"({"qtest":{"data":true}})")), "unknown configuration key 'qtest.data'");
    EXPECT_EQ(only_error(apply(R"({"qtest":[]})")), "configuration key 'qtest' must be an object");
    EXPECT_EQ(only_error(apply(R"({"gtest":true})")), "configuration key 'gtest' must be an object");
    EXPECT_EQ(only_error(apply(R"({"overrides":{"A::f":{"retuns":"int"}}})")),
              "unknown configuration key 'overrides.A::f.retuns'");
}

TEST(ConfigErrors, WrongTypes) {
    EXPECT_EQ(only_error(apply(R"({"std":11})")), "configuration key 'std' must be a string");
    EXPECT_EQ(only_error(apply(R"({"no_exceptions":"yes"})")),
              "configuration key 'no_exceptions' must be true or false");
    EXPECT_EQ(only_error(apply(R"({"naming":[]})")), "configuration key 'naming' must be an object");
    EXPECT_EQ(only_error(apply(R"({"ignore_macros":"X"})")), "configuration key 'ignore_macros' must be a list");
    EXPECT_EQ(only_error(apply(R"({"ignore_macros":[1]})")),
              "configuration key 'ignore_macros[0]' must be a string");
    EXPECT_EQ(only_error(apply(R"({"type_map":{"a":1}})")),
              "configuration key 'type_map.a' must be a string");
    EXPECT_EQ(only_error(apply(R"({"overrides":{"A::f":{"const":"yes"}}})")),
              "configuration key 'overrides.A::f.const' must be true or false");
}

TEST(ConfigErrors, ExceptionBaseNeedsAString) {
    EXPECT_EQ(only_error(apply(R"({"exception_base": 3})")), "configuration key 'exception_base' must be a string");
    EXPECT_EQ(only_error(apply(R"({"exception_base": ["std::exception"]})")), "configuration key 'exception_base' must be a string");
}

TEST(ConfigErrors, ValuesOutsideTheirSet) {
    EXPECT_EQ(only_error(apply(R"({"std":"c++98"})")),
              "configuration key 'std' must be one of: c++11, c++14, c++17, c++20, c++23 (got 'c++98')");
    EXPECT_EQ(only_error(apply(R"({"stub_policy":"hope"})")),
              "configuration key 'stub_policy' must be one of: throw, abort, default, hook (got 'hope')");
    EXPECT_EQ(only_error(apply(R"({"guard":"pragma"})")),
              "configuration key 'guard' must be one of: ifndef, pragma-once (got 'pragma')");
    EXPECT_EQ(only_error(apply(R"({"qt":4})")), "configuration key 'qt' must be 5 or 6");
    EXPECT_EQ(only_error(apply(R"({"qt":"6"})")), "configuration key 'qt' must be 5 or 6");
    EXPECT_EQ(only_error(apply(R"({"qt":6.5})")), "configuration key 'qt' must be 5 or 6");
}

TEST(ConfigErrors, NamingPatternsNeedTheirPlaceholder) {
    EXPECT_EQ(only_error(apply(R"({"naming":{"header":"all.h"}})")),
              "configuration key 'naming.header' must contain {stem} (got 'all.h')");
    EXPECT_EQ(only_error(apply(R"({"naming":{"suite":"{stem}Test"}})")),
              "configuration key 'naming.suite' uses unknown placeholder {stem}; only {Class} is available here");
    EXPECT_EQ(only_error(apply(R"({"naming":{"mock_file":"mocks.h"}})")),
              "configuration key 'naming.mock_file' must contain {stem} (got 'mocks.h')");
    EXPECT_EQ(only_error(apply(R"({"naming":{"mock_class":"Mock"}})")),
              "configuration key 'naming.mock_class' must contain {Class} (got 'Mock')");
    EXPECT_EQ(only_error(apply(R"({"naming":{"source":"{stem.cpp"}})")),
              "configuration key 'naming.source' has an unclosed '{' (got '{stem.cpp')");
    EXPECT_EQ(only_error(apply(R"({"naming":{"source":"stem}.cpp"}})")),
              "configuration key 'naming.source' has an unmatched '}' (got 'stem}.cpp')");
}

TEST(ConfigErrors, NamesAndIdentifiers) {
    EXPECT_EQ(only_error(apply(R"({"default_namespace":"a-b"})")),
              "configuration key 'default_namespace' must be empty or a namespace such as acme or acme::core (got 'a-b')");
    EXPECT_FALSE(apply(R"({"default_namespace":"::a"})").ok);
    EXPECT_FALSE(apply(R"({"default_namespace":"a::"})").ok);
    EXPECT_TRUE(apply(R"({"default_namespace":""})").ok);
    EXPECT_FALSE(apply(R"({"ignore_macros":["MY MACRO"]})").ok);
    EXPECT_FALSE(apply(R"({"ignore_macros":["9X"]})").ok);
    EXPECT_FALSE(apply(R"({"external_types":["a::"]})").ok);
    EXPECT_FALSE(apply(R"({"external_functions":[""]})").ok);
}

TEST(ConfigErrors, OverrideKeysAndParameterPositions) {
    EXPECT_EQ(only_error(apply(R"({"overrides":{"items":{"returns":"int"}}})")),
              "configuration key 'overrides.items' must name a member as Class::member or ns::Class::member");
    EXPECT_FALSE(apply(R"({"overrides":{"A::f":{"params":{"0":"int"}}}})").ok);
    EXPECT_FALSE(apply(R"({"overrides":{"A::f":{"params":{"01":"int"}}}})").ok);
    EXPECT_FALSE(apply(R"({"overrides":{"A::f":{"params":{"x":"int"}}}})").ok);
    EXPECT_FALSE(apply(R"({"overrides":{"A::f":{"params":[]}}})").ok);
    EXPECT_FALSE(apply(R"({"overrides":{"A::f":{"returns":"  "}}})").ok);
}

TEST(ConfigErrors, TemplateInstantiations) {
    EXPECT_FALSE(apply(R"({"template_instantiations":{"Box":[]}})").ok);
    EXPECT_FALSE(apply(R"({"template_instantiations":{"Box":["int","int"]}})").ok);
    EXPECT_FALSE(apply(R"({"template_instantiations":{"Box":"int"}})").ok);
    EXPECT_FALSE(apply(R"({"template_instantiations":{"Bo x":["int"]}})").ok);
}

TEST(ConfigErrors, TypeMapCollisionsAfterNormalisation) {
    EXPECT_FALSE(apply(R"({"type_map":{"unsigned int":"a","unsigned  int":"b"}})").ok);
    EXPECT_FALSE(apply(R"({"type_map":{"  ":"a"}})").ok);
}

TEST(ConfigErrors, FileHeaderMustBeAComment) {
    EXPECT_EQ(only_error(apply(R"({"file_header":"Copyright ACME"})")),
              "configuration key 'file_header' must be a C++ comment (start with // or /*)");
    EXPECT_TRUE(apply(R"({"file_header":"  // indented is fine"})").ok);
}

TEST(ConfigErrors, AllProblemsAreReportedInOnePass) {
    const Applied a = apply(R"({"std":"c++98","colour":1,"qt":3,"naming":{"suite":"x"}})");
    EXPECT_FALSE(a.ok);
    EXPECT_EQ(a.diagnostics.count(Severity::Error), 4u);
}

TEST(ConfigErrors, AFailedFileLeavesTheConfigUntouched) {
    Config start;
    start.cxx_std = CxxStd::Cxx11;
    start.guard = GuardStyle::PragmaOnce;

    // The first key is valid; the second is not. Nothing may be applied.
    const Applied a = apply(R"({"std":"c++20","stub_policy":"hope","by_value_types":["A"]})", start);
    EXPECT_FALSE(a.ok);
    EXPECT_EQ(a.config.cxx_std, CxxStd::Cxx11);
    EXPECT_EQ(a.config.guard, GuardStyle::PragmaOnce);
    EXPECT_TRUE(a.config.by_value_types.empty());
    EXPECT_EQ(a.config.stub_policy, StubPolicy::Throw);
}

// ---- helpers ----------------------------------------------------------------------------

TEST(ConfigHelpers, StringsRoundTrip) {
    for (CxxStd s : {CxxStd::Cxx11, CxxStd::Cxx14, CxxStd::Cxx17, CxxStd::Cxx20, CxxStd::Cxx23}) {
        EXPECT_EQ(parse_std(to_string(s)), s);
    }
    for (StubPolicy s : {StubPolicy::Throw, StubPolicy::Abort, StubPolicy::Default, StubPolicy::Hook}) {
        EXPECT_EQ(parse_stub_policy(to_string(s)), s);
    }
    for (Placeholder s : {Placeholder::Fail, Placeholder::Skip}) {
        EXPECT_EQ(parse_placeholder(to_string(s)), s);
    }
    for (AutoPolicy s : {AutoPolicy::Error, AutoPolicy::Todo}) {
        EXPECT_EQ(parse_auto_policy(to_string(s)), s);
    }
    for (GuardStyle s : {GuardStyle::Ifndef, GuardStyle::PragmaOnce}) {
        EXPECT_EQ(parse_guard_style(to_string(s)), s);
    }
    EXPECT_FALSE(parse_std("c++99").has_value());
    EXPECT_FALSE(parse_std("C++11").has_value()) << "spellings are case-sensitive";
}

TEST(ConfigHelpers, StandardNumbers) {
    EXPECT_EQ(to_number(CxxStd::Cxx11), 11);
    EXPECT_EQ(to_number(CxxStd::Cxx14), 14);
    EXPECT_EQ(to_number(CxxStd::Cxx17), 17);
    EXPECT_EQ(to_number(CxxStd::Cxx20), 20);
    EXPECT_EQ(to_number(CxxStd::Cxx23), 23);
}

TEST(ConfigHelpers, NoExceptionsTurnsThrowIntoAbortButKeepsDefault) {
    Config c;
    EXPECT_EQ(effective_stub_policy(c), StubPolicy::Throw);

    c.no_exceptions = true;
    EXPECT_EQ(effective_stub_policy(c), StubPolicy::Abort);

    c.stub_policy = StubPolicy::Default;
    EXPECT_EQ(effective_stub_policy(c), StubPolicy::Default);

    c.stub_policy = StubPolicy::Abort;
    EXPECT_EQ(effective_stub_policy(c), StubPolicy::Abort);
}

TEST(ConfigHelpers, NoStlTurnsThrowIntoAbortToo) {
    Config c;
    c.no_stl = true;
    EXPECT_EQ(effective_stub_policy(c), StubPolicy::Abort) << "there is no std::logic_error to throw";

    c.no_exceptions = true;
    EXPECT_EQ(effective_stub_policy(c), StubPolicy::Abort);

    c.no_exceptions = false;
    c.stub_policy = StubPolicy::Default;
    EXPECT_EQ(effective_stub_policy(c), StubPolicy::Default) << "an explicit default is kept";

    c.stub_policy = StubPolicy::Abort;
    EXPECT_EQ(effective_stub_policy(c), StubPolicy::Abort);

    c.no_stl = false;
    c.stub_policy = StubPolicy::Throw;
    EXPECT_EQ(effective_stub_policy(c), StubPolicy::Throw) << "and only no_stl, no_exceptions or both change it";
}

TEST(ConfigHelpers, HookIsKeptWhateverTheOtherPoliciesSay) {
    Config c;
    c.stub_policy = StubPolicy::Hook;
    EXPECT_EQ(effective_stub_policy(c), StubPolicy::Hook);
    c.no_exceptions = true;
    c.no_stl = true;
    EXPECT_EQ(effective_stub_policy(c), StubPolicy::Hook) << "only the default handler of the hook header changes";
}

TEST(ConfigKeys, NoStlIsABoolean) {
    EXPECT_TRUE(apply(R"({"no_stl":true})").config.no_stl);
    EXPECT_FALSE(apply(R"({"no_stl":false})").config.no_stl);
    EXPECT_EQ(only_error(apply(R"({"no_stl":"yes"})")), "configuration key 'no_stl' must be true or false");
}

// ---- files --------------------------------------------------------------------------------

TEST(ConfigFile, LoadsFromDisk) {
    mt::test::TempDir dir;
    const std::string path = mt::test::write_file(dir.path() / "mirrortest.json", R"({"std":"c++14"})");

    Config c;
    Diagnostics d;
    EXPECT_TRUE(load_config_file(c, path, d));
    EXPECT_EQ(c.cxx_std, CxxStd::Cxx14);
    EXPECT_TRUE(d.empty());
}

TEST(ConfigFile, ErrorsNameTheFile) {
    mt::test::TempDir dir;
    const std::string path = mt::test::write_file(dir.path() / "bad.json", R"({"colour":1})");

    Config c;
    Diagnostics d;
    EXPECT_FALSE(load_config_file(c, path, d));
    ASSERT_EQ(d.all().size(), 1u);
    EXPECT_EQ(d.all()[0].where.file, path);
}

TEST(ConfigFile, AMissingFileIsAnIoError) {
    mt::test::TempDir dir;
    Config c;
    Diagnostics d;
    EXPECT_FALSE(load_config_file(c, dir.file("nope.json"), d));
    ASSERT_EQ(d.all().size(), 1u);
    EXPECT_EQ(d.all()[0].code, code::io);
}
