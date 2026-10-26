// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// A configuration written in YAML: the same keys and the same errors as the JSON one, which is what it
// is turned into, and the few things YAML adds (scalars with a type, anchors, documents).

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

Applied apply_yaml(const std::string& yaml, Config start = {}) {
    Applied a{false, std::move(start), {}};
    a.ok = apply_config_yaml(a.config, yaml, "mt.yaml", a.diagnostics);
    return a;
}

Applied apply_json(const std::string& json) {
    Applied a{false, {}, {}};
    a.ok = apply_config_json(a.config, json, "mt.json", a.diagnostics);
    return a;
}

// The single diagnostic a YAML produced: its message, or "" if there was none or several.
std::string only_message(const Applied& a) {
    if (a.diagnostics.all().size() != 1) return "";
    EXPECT_EQ(a.diagnostics.all()[0].code, code::config);
    EXPECT_EQ(a.diagnostics.all()[0].severity, Severity::Error);
    EXPECT_EQ(a.diagnostics.all()[0].where.file, "mt.yaml");
    return a.diagnostics.all()[0].message;
}

std::string all_messages(const Applied& a) {
    std::string out;
    for (const Diagnostic& d : a.diagnostics.all()) out += (out.empty() ? "" : " | ") + d.message;
    return out;
}

void expect_same(const Config& a, const Config& b) {
    EXPECT_EQ(a.cxx_std, b.cxx_std);
    EXPECT_EQ(a.default_namespace, b.default_namespace);
    EXPECT_EQ(a.naming.header, b.naming.header);
    EXPECT_EQ(a.naming.source, b.naming.source);
    EXPECT_EQ(a.naming.gtest_file, b.naming.gtest_file);
    EXPECT_EQ(a.naming.qtest_file, b.naming.qtest_file);
    EXPECT_EQ(a.naming.suite, b.naming.suite);
    EXPECT_EQ(a.naming.qtest_class, b.naming.qtest_class);
    EXPECT_EQ(a.naming.mock_file, b.naming.mock_file);
    EXPECT_EQ(a.naming.mock_class, b.naming.mock_class);
    EXPECT_EQ(a.gtest.fixture, b.gtest.fixture);
    EXPECT_EQ(a.gtest.mocks, b.gtest.mocks);
    EXPECT_EQ(a.gtest.parameterised, b.gtest.parameterised);
    EXPECT_EQ(a.qtest.data_driven, b.qtest.data_driven);
    EXPECT_EQ(a.guard, b.guard);
    EXPECT_EQ(a.file_header, b.file_header);
    EXPECT_EQ(a.stub_policy, b.stub_policy);
    EXPECT_EQ(a.no_exceptions, b.no_exceptions);
    EXPECT_EQ(a.no_stl, b.no_stl);
    EXPECT_EQ(a.no_rtti, b.no_rtti);
    EXPECT_EQ(a.placeholder, b.placeholder);
    EXPECT_EQ(a.auto_policy, b.auto_policy);
    EXPECT_EQ(a.placeholder_type, b.placeholder_type);
    EXPECT_EQ(a.string_literal_param, b.string_literal_param);
    EXPECT_EQ(a.qt, b.qt);
    EXPECT_EQ(a.exception_base, b.exception_base);
    EXPECT_EQ(a.type_map, b.type_map);
    EXPECT_EQ(a.by_value_types, b.by_value_types);
    EXPECT_EQ(a.default_constructible_types, b.default_constructible_types);
    EXPECT_EQ(a.external_types, b.external_types);
    EXPECT_EQ(a.external_functions, b.external_functions);
    EXPECT_EQ(a.ignore_macros, b.ignore_macros);
    ASSERT_EQ(a.overrides.size(), b.overrides.size());
    for (const auto& [name, o] : a.overrides) {
        const auto other = b.overrides.find(name);
        ASSERT_NE(other, b.overrides.end()) << name;
        EXPECT_EQ(o.returns, other->second.returns) << name;
        EXPECT_EQ(o.params, other->second.params) << name;
        EXPECT_EQ(o.is_const, other->second.is_const) << name;
        EXPECT_EQ(o.is_static, other->second.is_static) << name;
        EXPECT_EQ(o.is_virtual, other->second.is_virtual) << name;
        EXPECT_EQ(o.is_noexcept, other->second.is_noexcept) << name;
    }
    EXPECT_EQ(a.template_instantiations, b.template_instantiations);
}

#define SKIP_WITHOUT_YAML() \
    if (!yaml_supported()) GTEST_SKIP() << "built without YAML"

// ---- the same keys ------------------------------------------------------------------------

TEST(ConfigYaml, ASupportedBuildIsToldSoAndAnUnsupportedOneRefusesWithAClearError) {
    if (yaml_supported()) {
        EXPECT_TRUE(apply_yaml("std: c++11\n").ok);
        return;
    }
    const Applied a = apply_yaml("std: c++11\n");
    EXPECT_FALSE(a.ok);
    EXPECT_EQ(only_message(a), "this build of mirrortest cannot read YAML; write the configuration as JSON, or build with -DMT_WITH_YAML=ON");
    EXPECT_EQ(a.config.cxx_std, Config().cxx_std) << "and nothing changed";
}

TEST(ConfigYaml, EveryKeyIsReadAsTheJsonOfTheSameMeaningIs) {
    SKIP_WITHOUT_YAML();
    const Applied from_json = apply_json(R"({
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
        "stub_policy": "hook",
        "no_exceptions": true,
        "no_stl": true,
        "no_rtti": true,
        "placeholder": "skip",
        "auto_policy": "todo",
        "placeholder_type": "long",
        "string_literal_param": "std::string",
        "qt": 5,
        "exception_base": "std::runtime_error",
        "type_map": { "unsigned int": "std::uint32_t" },
        "by_value_types": ["Handle"],
        "default_constructible_types": ["Handle"],
        "external_types": ["Hal", "board::Pin"],
        "external_functions": ["delay_ms", "board::reset"],
        "ignore_macros": ["MY_API", "MY_ATTR"],
        "overrides": { "acme::Foo::items": { "returns": "std::vector<int>", "params": { "1": "int" }, "const": true } },
        "template_instantiations": { "acme::Box": ["int", "std::string"] }
    })");
    ASSERT_TRUE(from_json.ok) << from_json.diagnostics.all()[0].message;

    const Applied from_yaml = apply_yaml(R"(# the house style
std: c++11
default_namespace: acme::core
naming:
  header: "include/{stem}.h"
  source: "src/{stem}.cpp"
  gtest_file: "{stem}_tests.cpp"
  qtest_file: "qt_{stem}.cpp"
  suite: "Test{Class}"
  qtest_class: "{Class}Spec"
  mock_file: "mocks/{stem}_mock.h"
  mock_class: "{Class}Mock"
gtest: { fixture: true, mocks: true, parameterised: true }
qtest:
  data_driven: true
guard: pragma-once
file_header: |
  // Copyright ACME
  // All rights reserved
stub_policy: hook
no_exceptions: true
no_stl: true
no_rtti: true
placeholder: skip
auto_policy: todo
placeholder_type: long
string_literal_param: std::string
qt: 5
exception_base: std::runtime_error
type_map:
  unsigned int: std::uint32_t
by_value_types: [Handle]
default_constructible_types:
  - Handle
external_types: [Hal, board::Pin]
external_functions: [delay_ms, board::reset]
ignore_macros: [MY_API, MY_ATTR]
overrides:
  acme::Foo::items:
    returns: std::vector<int>
    params: { 1: int }
    const: true
template_instantiations:
  acme::Box: [int, std::string]
)");
    ASSERT_TRUE(from_yaml.ok) << from_yaml.diagnostics.all()[0].message;
    EXPECT_TRUE(from_yaml.diagnostics.empty());
    expect_same(from_yaml.config, from_json.config);
    EXPECT_EQ(from_yaml.config.file_header, "// Copyright ACME\n// All rights reserved") << "a block scalar, trimmed like the JSON one";
}

TEST(ConfigYaml, OnlyTheKeysPresentChangeAndAnEmptyDocumentChangesNothing) {
    SKIP_WITHOUT_YAML();
    Config start;
    start.cxx_std = CxxStd::Cxx20;
    start.by_value_types = {"A"};
    const Applied some = apply_yaml("guard: pragma-once\n", start);
    ASSERT_TRUE(some.ok);
    EXPECT_EQ(some.config.guard, GuardStyle::PragmaOnce);
    EXPECT_EQ(some.config.cxx_std, CxxStd::Cxx20);
    EXPECT_EQ(some.config.by_value_types, std::vector<std::string>{"A"});

    for (const char* text : {"", "\n", "# nothing but a comment\n", "---\n", "~\n", "null\n", "--- # a document with nothing in it\n...\n"}) {
        const Applied none = apply_yaml(text, start);
        EXPECT_TRUE(none.ok) << text;
        EXPECT_TRUE(none.diagnostics.empty()) << text;
        EXPECT_EQ(none.config.cxx_std, CxxStd::Cxx20) << text;
    }
}

// ---- scalars ------------------------------------------------------------------------------

TEST(ConfigYaml, APlainScalarHasATypeAndAQuotedOneIsAString) {
    SKIP_WITHOUT_YAML();
    EXPECT_TRUE(apply_yaml("no_stl: true\n").config.no_stl);
    EXPECT_TRUE(apply_yaml("no_stl: True\n").config.no_stl);
    EXPECT_TRUE(apply_yaml("no_stl: TRUE\n").config.no_stl);
    EXPECT_FALSE(apply_yaml("no_stl: false\n").config.no_stl);
    EXPECT_EQ(apply_yaml("qt: 5\n").config.qt, 5);
    EXPECT_EQ(apply_yaml("qt: +6\n").config.qt, 6);

    EXPECT_EQ(only_message(apply_yaml("qt: \"5\"\n")), "configuration key 'qt' must be 5 or 6") << "quoted, so a string";
    EXPECT_EQ(only_message(apply_yaml("qt: '5'\n")), "configuration key 'qt' must be 5 or 6");
    EXPECT_EQ(only_message(apply_yaml("no_stl: \"true\"\n")), "configuration key 'no_stl' must be true or false");
    EXPECT_EQ(only_message(apply_yaml("no_stl: !!str true\n")), "configuration key 'no_stl' must be true or false") << "and so is !!str";
}

TEST(ConfigYaml, YesAndOnAreNotBooleansBecauseYamlOneDotOneIsNotTheRule) {
    SKIP_WITHOUT_YAML();
    for (const char* word : {"yes", "no", "on", "off", "Yes", "y", "n"}) {
        EXPECT_EQ(only_message(apply_yaml(std::string("no_stl: ") + word + "\n")), "configuration key 'no_stl' must be true or false") << word;
    }
}

TEST(ConfigYaml, ValuesThatLookLikeOtherThingsStayStringsWhereAStringIsWanted) {
    SKIP_WITHOUT_YAML();
    EXPECT_EQ(apply_yaml("std: c++17\n").config.cxx_std, CxxStd::Cxx17);
    EXPECT_EQ(apply_yaml("placeholder_type: 1.5\n").config.placeholder_type, "1.5") << "a float is text";
    EXPECT_EQ(apply_yaml("placeholder_type: 0x10\n").config.placeholder_type, "0x10");
    EXPECT_EQ(apply_yaml("placeholder_type: 99999999999999999999\n").config.placeholder_type, "99999999999999999999") << "too big for a number";
    EXPECT_EQ(apply_yaml("default_namespace: acme::detail\n").config.default_namespace, "acme::detail");
    EXPECT_EQ(only_message(apply_yaml("qt: ~\n")), "configuration key 'qt' must be 5 or 6") << "null is not a number";
    EXPECT_EQ(only_message(apply_yaml("qt:\n")), "configuration key 'qt' must be 5 or 6") << "nor is nothing";
}

TEST(ConfigYaml, ABlockScalarKeepsItsLines) {
    SKIP_WITHOUT_YAML();
    const Applied folded = apply_yaml("file_header: >\n  // one\n  // two\n");
    ASSERT_TRUE(folded.ok);
    EXPECT_EQ(folded.config.file_header, "// one // two") << "folded, as YAML says";
    const Applied literal = apply_yaml("file_header: |-\n  /* one\n     two */\n");
    ASSERT_TRUE(literal.ok);
    EXPECT_EQ(literal.config.file_header, "/* one\n   two */");
}

// ---- the YAML of it -----------------------------------------------------------------------

TEST(ConfigYaml, AnchorsAndAliasesShareWhatTheyName) {
    SKIP_WITHOUT_YAML();
    const Applied a = apply_yaml("by_value_types: &handles [Handle, Token]\ndefault_constructible_types: *handles\nexternal_types: [*handles, Hal]\n");
    EXPECT_FALSE(a.ok) << "an alias inside a list of strings is a list there, and a list is not a type name";
    const Applied b = apply_yaml("by_value_types: &handles [Handle, Token]\ndefault_constructible_types: *handles\n");
    ASSERT_TRUE(b.ok) << b.diagnostics.all()[0].message;
    EXPECT_EQ(b.config.default_constructible_types, (std::vector<std::string>{"Handle", "Token"}));
    EXPECT_EQ(b.config.by_value_types, b.config.default_constructible_types);
}

TEST(ConfigYaml, FlowStyleCommentsAndDocumentMarkersAreYaml) {
    SKIP_WITHOUT_YAML();
    const Applied a = apply_yaml("--- # start\n{ std: c++14, gtest: { mocks: true } }  # flow\n...\n");
    ASSERT_TRUE(a.ok) << a.diagnostics.all()[0].message;
    EXPECT_EQ(a.config.cxx_std, CxxStd::Cxx14);
    EXPECT_TRUE(a.config.gtest.mocks);
}

TEST(ConfigYaml, ASyntaxErrorIsLocatedAtTheLineAndColumn) {
    SKIP_WITHOUT_YAML();
    const Applied a = apply_yaml("std: c++17\nnaming: [unclosed\nguard: ifndef\n");
    EXPECT_FALSE(a.ok);
    ASSERT_EQ(a.diagnostics.all().size(), 1u);
    const Diagnostic& d = a.diagnostics.all()[0];
    EXPECT_EQ(d.code, code::config);
    EXPECT_EQ(d.message.rfind("invalid YAML: ", 0), 0u) << d.message;
    EXPECT_EQ(d.where.file, "mt.yaml");
    EXPECT_GE(d.where.line, 2u);
    EXPECT_LE(d.where.line, 3u);
    EXPECT_GT(d.where.column, 0u);
    EXPECT_EQ(a.config.cxx_std, Config().cxx_std) << "and nothing was applied";
    EXPECT_NE(format_diagnostic(d).find("mt.yaml:"), std::string::npos);
}

// ---- what is refused ----------------------------------------------------------------------

TEST(ConfigYaml, TheKeysOfAMappingAreWordsAndEachOnceOnly) {
    SKIP_WITHOUT_YAML();
    const Applied twice = apply_yaml("std: c++11\nstd: c++14\n");
    EXPECT_EQ(only_message(twice), "the key 'std' appears twice in the same mapping");
    ASSERT_EQ(twice.diagnostics.all().size(), 1u);
    EXPECT_EQ(twice.diagnostics.all()[0].where.line, 2u);
    EXPECT_EQ(twice.diagnostics.all()[0].where.column, 1u);

    EXPECT_EQ(only_message(apply_yaml("naming:\n  header: a\n  header: b\n")), "the key 'header' appears twice in the same mapping");
    EXPECT_EQ(only_message(apply_yaml("? [a, b]\n: c\n")), "a key must be a plain word or a string");
}

TEST(ConfigYaml, ADocumentIsOneMappingOfKeysToValues) {
    SKIP_WITHOUT_YAML();
    for (const char* text : {"- a\n- b\n", "just text\n", "42\n", "true\n"}) {
        EXPECT_EQ(only_message(apply_yaml(text)), "a configuration is a YAML mapping of keys to values") << text;
    }
    EXPECT_EQ(only_message(apply_yaml("std: c++11\n---\nstd: c++14\n")), "a configuration is one YAML document, and this has 2");
}

TEST(ConfigYaml, OnlyTheStringTagIsAccepted) {
    SKIP_WITHOUT_YAML();
    EXPECT_EQ(only_message(apply_yaml("qt: !!int 5\n")), "the tag 'tag:yaml.org,2002:int' is not supported; only !!str is");
    EXPECT_EQ(only_message(apply_yaml("std: !mine c++11\n")), "the tag '!mine' is not supported; only !!str is");
    EXPECT_TRUE(apply_yaml("std: !!str c++11\n").ok);
}

TEST(ConfigYaml, MergeKeysAreNotSupportedAndSayWhichKeyIsUnknown) {
    SKIP_WITHOUT_YAML();
    EXPECT_EQ(only_message(apply_yaml("naming:\n  <<: { header: a }\n")), "unknown configuration key 'naming.<<'");
    // The anchor's own key is unknown to the reader as well, and both are reported at once.
    const Applied merged = apply_yaml("base: &b { header: a }\nnaming:\n  <<: *b\n  source: \"{stem}.cc\"\n");
    EXPECT_FALSE(merged.ok);
    ASSERT_EQ(merged.diagnostics.all().size(), 2u) << all_messages(merged);
    EXPECT_EQ(merged.diagnostics.all()[0].message, "unknown configuration key 'base'");
    EXPECT_EQ(merged.diagnostics.all()[1].message, "unknown configuration key 'naming.<<'");
}

TEST(ConfigYaml, AnAliasThatKeepsExpandingIsStoppedNotFollowed) {
    SKIP_WITHOUT_YAML();
    std::string bomb = "l0: &l0 [x, x, x, x, x, x, x, x, x, x]\n";
    for (int i = 1; i <= 8; ++i) {
        const std::string prev = "*l" + std::to_string(i - 1);
        bomb += "l" + std::to_string(i) + ": &l" + std::to_string(i) + " [";
        for (int k = 0; k < 10; ++k) bomb += (k ? ", " : "") + prev;
        bomb += "]\n";
    }
    const Applied a = apply_yaml(bomb);
    EXPECT_FALSE(a.ok);
    ASSERT_EQ(a.diagnostics.all().size(), 1u);
    EXPECT_EQ(a.diagnostics.all()[0].message.rfind("the document expands to more than 100000 nodes", 0), 0u) << a.diagnostics.all()[0].message;
}

TEST(ConfigYaml, ASequenceThatContainsItselfIsStopped) {
    SKIP_WITHOUT_YAML();
    const Applied a = apply_yaml("by_value_types: &x [*x]\n");
    EXPECT_FALSE(a.ok);
    ASSERT_EQ(a.diagnostics.all().size(), 1u);
    EXPECT_NE(a.diagnostics.all()[0].message.find("levels deep"), std::string::npos) << a.diagnostics.all()[0].message;
}

TEST(ConfigYaml, TheErrorsOfTheKeysAreTheOnesOfTheJsonReader) {
    SKIP_WITHOUT_YAML();
    EXPECT_EQ(only_message(apply_yaml("colour: red\n")), "unknown configuration key 'colour'");
    const Applied from_json = apply_json(R"({"std": "c++98"})");
    ASSERT_EQ(from_json.diagnostics.all().size(), 1u);
    EXPECT_EQ(only_message(apply_yaml("std: c++98\n")), from_json.diagnostics.all()[0].message)
        << "the same words as the JSON file gives for the same mistake";
    const Applied bad = apply_yaml("stub_policy: hope\n");
    EXPECT_EQ(bad.diagnostics.all().size(), 1u);
    EXPECT_EQ(bad.diagnostics.all()[0].message, "configuration key 'stub_policy' must be one of: throw, abort, default, hook (got 'hope')");
    EXPECT_EQ(only_message(apply_yaml("naming: [a]\n")), "configuration key 'naming' must be an object");

    const Applied two = apply_yaml("colour: red\nqt: 4\n");
    EXPECT_FALSE(two.ok);
    EXPECT_EQ(two.diagnostics.all().size(), 2u) << "all of them, at once, like the JSON";
}

// ---- files ------------------------------------------------------------------------------------

TEST(ConfigYamlFiles, TheExtensionDecidesTheReader) {
    SKIP_WITHOUT_YAML();
    mt::test::TempDir dir;
    mt::test::write_file(dir.path() / "a.yaml", "std: c++11\n");
    mt::test::write_file(dir.path() / "b.yml", "std: c++14\n");
    mt::test::write_file(dir.path() / "c.YAML", "std: c++20\n");
    mt::test::write_file(dir.path() / "d.json", R"({"std": "c++23"})");
    mt::test::write_file(dir.path() / "e.conf", R"({"std": "c++11"})");
    for (const auto& [file, expected] : std::vector<std::pair<const char*, CxxStd>>{
             {"a.yaml", CxxStd::Cxx11}, {"b.yml", CxxStd::Cxx14}, {"c.YAML", CxxStd::Cxx20}, {"d.json", CxxStd::Cxx23}, {"e.conf", CxxStd::Cxx11}}) {
        Config c;
        Diagnostics d;
        ASSERT_TRUE(load_config_file(c, file, d, dir.path())) << file << (d.all().empty() ? "" : d.all()[0].message);
        EXPECT_EQ(c.cxx_std, expected) << file;
    }
}

TEST(ConfigYamlFiles, YamlInAJsonFileIsJsonsErrorAndTheOtherWayRoundIsYamls) {
    SKIP_WITHOUT_YAML();
    mt::test::TempDir dir;
    mt::test::write_file(dir.path() / "wrong.json", "std: c++11\n");
    Config c;
    Diagnostics d;
    EXPECT_FALSE(load_config_file(c, "wrong.json", d, dir.path()));
    ASSERT_EQ(d.all().size(), 1u);
    EXPECT_EQ(d.all()[0].message.rfind("invalid JSON: ", 0), 0u);

    // JSON with comments is not YAML (// is not a comment there) but plain JSON without them is a flow mapping.
    mt::test::write_file(dir.path() / "plain.yaml", R"({"std": "c++14", "gtest": {"mocks": true}})");
    Config y;
    Diagnostics yd;
    ASSERT_TRUE(load_config_file(y, "plain.yaml", yd, dir.path())) << (yd.all().empty() ? "" : yd.all()[0].message);
    EXPECT_EQ(y.cxx_std, CxxStd::Cxx14);
    EXPECT_TRUE(y.gtest.mocks);
}

TEST(ConfigYamlFiles, AMissingFileIsAnIoErrorWhateverItsExtension) {
    mt::test::TempDir dir;
    Config c;
    Diagnostics d;
    EXPECT_FALSE(load_config_file(c, "nope.yaml", d, dir.path()));
    ASSERT_EQ(d.all().size(), 1u);
    EXPECT_EQ(d.all()[0].code, code::io);
}

TEST(ConfigYamlFiles, WithoutYamlTheFileIsReadAndThenRefusedByName) {
    if (yaml_supported()) GTEST_SKIP() << "built with YAML";
    mt::test::TempDir dir;
    mt::test::write_file(dir.path() / "a.yaml", "std: c++11\n");
    Config c;
    Diagnostics d;
    EXPECT_FALSE(load_config_file(c, "a.yaml", d, dir.path()));
    ASSERT_EQ(d.all().size(), 1u);
    EXPECT_EQ(d.all()[0].where.file, "a.yaml");
    EXPECT_NE(d.all()[0].message.find("-DMT_WITH_YAML=ON"), std::string::npos);
}

}  // namespace
