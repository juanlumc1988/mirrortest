// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "mirrortest/core/templates.h"
#include "test_support.h"

using namespace mt::core;
using nlohmann::json;
using mt::test::TempDir;
using mt::test::write_file;

namespace {

// Static storage: EmbeddedTemplate only holds views.
const char kList[] =
    "class {{ name }} {\n"
    "{% for m in methods %}\n"
    "    void {{ m }}();\n"
    "{% endfor %}\n"
    "};\n";
const char kGreeting[] = "Hello {{ who }}\n";
const char kIncludes[] = "[{% include \"b.inja\" %}]\n";
const char kPart[] = "<{{ who }}>";
const char kBroken1[] = "first\n{% if x %}\nnever closed\n";
const char kBroken2[] = "{% for %}\n";

std::vector<EmbeddedTemplate> sample() {
    return {{"list.inja", kList}, {"greeting.inja", kGreeting}};
}

std::string render_ok(const TemplateStore& store, const char* name, const json& data) {
    Diagnostics d;
    const auto out = store.render(name, data, d);
    EXPECT_TRUE(out.has_value()) << (d.all().empty() ? "" : d.all()[0].message);
    EXPECT_TRUE(d.empty());
    return out.value_or("");
}

}  // namespace

// ---- rendering ----------------------------------------------------------------------

TEST(TemplateStore, RendersVariablesAndLoopsWithoutStrayBlankLines) {
    Diagnostics d;
    const TemplateStore store(sample(), std::nullopt, d);
    EXPECT_EQ(render_ok(store, "list.inja", {{"name", "Foo"}, {"methods", json::array({"size", "empty"})}}),
              "class Foo {\n"
              "    void size();\n"
              "    void empty();\n"
              "};\n");
    EXPECT_EQ(render_ok(store, "greeting.inja", {{"who", "world"}}), "Hello world\n");
}

TEST(TemplateStore, RenderingIsDeterministic) {
    Diagnostics d;
    const TemplateStore store(sample(), std::nullopt, d);
    const json data = {{"name", "Foo"}, {"methods", json::array({"a", "b", "c"})}};
    EXPECT_EQ(render_ok(store, "list.inja", data), render_ok(store, "list.inja", data));
}

TEST(TemplateStore, ATemplateCanIncludeAnotherFromTheStore) {
    Diagnostics d;
    const TemplateStore store({{"a.inja", kIncludes}, {"b.inja", kPart}}, std::nullopt, d);
    EXPECT_EQ(render_ok(store, "a.inja", {{"who", "x"}}), "[<x>]\n");
}

TEST(TemplateStore, IncludesNeverComeFromTheWorkingDirectory) {
    // A stray file next to wherever the tool is run must not be picked up.
    TempDir dir;
    write_file(dir.path() / "b.inja", "FROM DISK");
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(dir.path());

    Diagnostics d;
    const TemplateStore store({{"a.inja", kIncludes}}, std::nullopt, d);
    const auto out = store.render("a.inja", json::object(), d);
    std::filesystem::current_path(previous);

    EXPECT_FALSE(out.has_value());
    EXPECT_TRUE(d.has_errors());
}

TEST(TemplateStore, KnowsItsTemplates) {
    Diagnostics d;
    const TemplateStore store({{"z.inja", kPart}, {"a/b.inja", kPart}, {"a.inja", kPart}}, std::nullopt, d);
    EXPECT_TRUE(store.has("z.inja"));
    EXPECT_TRUE(store.has("a/b.inja"));
    EXPECT_FALSE(store.has("nope.inja"));
    EXPECT_EQ(store.names(), (std::vector<std::string>{"a.inja", "a/b.inja", "z.inja"}));
    EXPECT_TRUE(store.overridden().empty());
}

TEST(TemplateStore, SurvivesBeingMoved) {
    Diagnostics d;
    TemplateStore first(sample(), std::nullopt, d);
    TemplateStore second(std::move(first));
    EXPECT_EQ(render_ok(second, "greeting.inja", {{"who", "moved"}}), "Hello moved\n");
}

// ---- errors ------------------------------------------------------------------------------

TEST(TemplateStoreErrors, AnUndefinedVariableIsAnErrorNamingTheTemplate) {
    Diagnostics d;
    const TemplateStore store(sample(), std::nullopt, d);
    EXPECT_FALSE(store.render("greeting.inja", json::object(), d).has_value());

    ASSERT_EQ(d.all().size(), 1u);
    EXPECT_EQ(d.all()[0].severity, Severity::Error);
    EXPECT_EQ(d.all()[0].code, code::config);
    EXPECT_EQ(d.all()[0].message.rfind("template 'greeting.inja': ", 0), 0u) << d.all()[0].message;
    EXPECT_NE(d.all()[0].message.find("who"), std::string::npos) << d.all()[0].message;
    EXPECT_EQ(d.all()[0].where.file, "(built-in) greeting.inja");
}

TEST(TemplateStoreErrors, AnUnknownTemplateIsAnError) {
    Diagnostics d;
    const TemplateStore store(sample(), std::nullopt, d);
    EXPECT_FALSE(store.render("nope.inja", json::object(), d).has_value());
    ASSERT_EQ(d.all().size(), 1u);
    EXPECT_EQ(d.all()[0].message, "there is no template named 'nope.inja'");
}

TEST(TemplateStoreErrors, AMissingIncludeIsAnError) {
    Diagnostics d;
    const TemplateStore store({{"a.inja", kIncludes}}, std::nullopt, d);
    EXPECT_FALSE(store.render("a.inja", {{"who", "x"}}, d).has_value());
    ASSERT_EQ(d.all().size(), 1u);
    EXPECT_NE(d.all()[0].message.find("b.inja"), std::string::npos) << d.all()[0].message;
}

TEST(TemplateStoreErrors, ATemplateThatDoesNotParseNamesItselfAndWhereItWentWrong) {
    Diagnostics d;
    const TemplateStore store({{"bad.inja", kBroken1}, {"ok.inja", kGreeting}}, std::nullopt, d);
    EXPECT_FALSE(store.validate(d));

    ASSERT_EQ(d.all().size(), 1u);
    EXPECT_EQ(d.all()[0].message.rfind("template 'bad.inja': ", 0), 0u) << d.all()[0].message;
    EXPECT_EQ(d.all()[0].where.file, "(built-in) bad.inja");
    EXPECT_GT(d.all()[0].where.line, 0u) << "inja knows the line of a parse error";
}

TEST(TemplateStoreErrors, EveryBrokenTemplateIsReportedAtOnce) {
    Diagnostics d;
    const TemplateStore store({{"b1.inja", kBroken1}, {"b2.inja", kBroken2}, {"ok.inja", kGreeting}},
                              std::nullopt, d);
    EXPECT_FALSE(store.validate(d));
    EXPECT_EQ(d.count(Severity::Error), 2u);
}

TEST(TemplateStoreErrors, ABrokenTemplateIsReportedOnEveryCallNotJustTheFirst) {
    Diagnostics first;
    Diagnostics second;
    const TemplateStore store({{"bad.inja", kBroken1}}, std::nullopt, first);
    EXPECT_FALSE(store.render("bad.inja", json::object(), first).has_value());
    EXPECT_FALSE(store.render("bad.inja", json::object(), second).has_value());
    EXPECT_EQ(first.count(Severity::Error), 1u);
    EXPECT_EQ(second.count(Severity::Error), 1u) << "a second failure must not be silent";
}

TEST(TemplateStoreErrors, WhileOneTemplateIsBrokenNoneRenders) {
    // Loud on purpose: the broken one would fail in the same run anyway, and a
    // half-working template set hides which one the user still has to fix.
    Diagnostics d;
    const TemplateStore store({{"bad.inja", kBroken1}, {"ok.inja", kGreeting}}, std::nullopt, d);
    EXPECT_FALSE(store.render("ok.inja", {{"who", "x"}}, d).has_value());
    EXPECT_TRUE(d.has_errors());
}

// ---- overriding from a directory --------------------------------------------------------------

TEST(TemplateStoreOverride, AFileReplacesTheEmbeddedTemplateOfTheSameName) {
    TempDir dir;
    write_file(dir.path() / "greeting.inja", "Howdy {{ who }}!\n");

    Diagnostics d;
    const TemplateStore store(sample(), dir.path(), d);
    EXPECT_TRUE(d.empty());
    EXPECT_EQ(render_ok(store, "greeting.inja", {{"who", "pardner"}}), "Howdy pardner!\n");
    EXPECT_EQ(store.overridden(), std::vector<std::string>{"greeting.inja"});
}

TEST(TemplateStoreOverride, TheTemplatesNotOverriddenStayEmbedded) {
    TempDir dir;
    write_file(dir.path() / "greeting.inja", "Howdy {{ who }}!\n");

    Diagnostics d;
    const TemplateStore store(sample(), dir.path(), d);
    EXPECT_EQ(render_ok(store, "list.inja", {{"name", "Foo"}, {"methods", json::array()}}),
              "class Foo {\n};\n");
}

TEST(TemplateStoreOverride, NestedNamesMapToSubdirectories) {
    TempDir dir;
    write_file(dir.path() / "impl" / "header.inja", "// mine {{ who }}\n");

    Diagnostics d;
    const TemplateStore store({{"impl/header.inja", kPart}}, dir.path(), d);
    EXPECT_EQ(render_ok(store, "impl/header.inja", {{"who", "x"}}), "// mine x\n");
    EXPECT_EQ(store.overridden(), std::vector<std::string>{"impl/header.inja"});
}

TEST(TemplateStoreOverride, AnOverrideCanIncludeAnEmbeddedTemplate) {
    TempDir dir;
    write_file(dir.path() / "a.inja", "<<{% include \"b.inja\" %}>>");

    Diagnostics d;
    const TemplateStore store({{"a.inja", kIncludes}, {"b.inja", kPart}}, dir.path(), d);
    EXPECT_EQ(render_ok(store, "a.inja", {{"who", "x"}}), "<<<x>>>");
}

TEST(TemplateStoreOverride, LineEndingsAreNormalisedToLf) {
    TempDir dir;
    write_file(dir.path() / "greeting.inja", "one\r\ntwo {{ who }}\r\n");

    Diagnostics d;
    const TemplateStore store(sample(), dir.path(), d);
    EXPECT_EQ(render_ok(store, "greeting.inja", {{"who", "x"}}), "one\ntwo x\n");
}

TEST(TemplateStoreOverride, ErrorsInAnOverridePointAtTheFile) {
    TempDir dir;
    const std::string path = write_file(dir.path() / "greeting.inja", "Hello {{ nobody }}\n");

    Diagnostics d;
    const TemplateStore store(sample(), dir.path(), d);
    EXPECT_FALSE(store.render("greeting.inja", json::object(), d).has_value());
    ASSERT_EQ(d.all().size(), 1u);
    EXPECT_EQ(d.all()[0].where.file, path);
}

TEST(TemplateStoreOverride, AFileThatReplacesNothingIsReportedAndIgnored) {
    TempDir dir;
    write_file(dir.path() / "greting.inja", "typo\n");                  // misspelt
    write_file(dir.path() / "sub" / "extra.inja", "nested stray\n");
    write_file(dir.path() / "notes.txt", "not a template\n");           // silently ignored

    Diagnostics d;
    const TemplateStore store(sample(), dir.path(), d);

    ASSERT_EQ(d.all().size(), 2u);
    EXPECT_EQ(d.all()[0].severity, Severity::Warning);
    EXPECT_EQ(d.all()[0].code, code::config);
    EXPECT_EQ(d.all()[0].message, "'greting.inja' does not replace any template and is ignored");
    EXPECT_EQ(d.all()[1].message, "'sub/extra.inja' does not replace any template and is ignored");
    EXPECT_TRUE(store.overridden().empty());
    EXPECT_EQ(store.names(), (std::vector<std::string>{"greeting.inja", "list.inja"}));
}

TEST(TemplateStoreOverride, ADirectoryNamedLikeATemplateIsNotAnOverride) {
    TempDir dir;
    std::filesystem::create_directory(dir.path() / "greeting.inja");

    Diagnostics d;
    const TemplateStore store(sample(), dir.path(), d);
    EXPECT_TRUE(store.overridden().empty());
    EXPECT_EQ(render_ok(store, "greeting.inja", {{"who", "x"}}), "Hello x\n");
}

TEST(TemplateStoreOverride, ADirectoryThatDoesNotExistIsAnErrorButTheStoreStillWorks) {
    TempDir dir;
    const std::filesystem::path missing = dir.path() / "nope";

    Diagnostics d;
    const TemplateStore store(sample(), missing, d);
    ASSERT_EQ(d.all().size(), 1u);
    EXPECT_EQ(d.all()[0].severity, Severity::Error);
    EXPECT_EQ(d.all()[0].code, code::io);
    EXPECT_EQ(d.all()[0].where.file, missing.string());

    Diagnostics later;
    EXPECT_TRUE(store.render("greeting.inja", {{"who", "x"}}, later).has_value());
}

TEST(TemplateStoreOverride, ARegularFileInsteadOfADirectoryIsAnError) {
    TempDir dir;
    const std::string file = write_file(dir.path() / "templates", "x");
    Diagnostics d;
    const TemplateStore store(sample(), std::filesystem::path(file), d);
    EXPECT_TRUE(d.has_errors());
}

// ---- the templates that ship ----------------------------------------------------------------------

TEST(EmbeddedTemplates, AreWellFormed) {
    const std::vector<EmbeddedTemplate> all = embedded_templates();
    ASSERT_FALSE(all.empty());

    std::vector<std::string> names;
    for (const EmbeddedTemplate& t : all) {
        names.emplace_back(t.name);
        EXPECT_TRUE(t.name.size() > 5 && t.name.substr(t.name.size() - 5) == ".inja") << t.name;
        EXPECT_FALSE(t.text.empty()) << t.name;
        EXPECT_EQ(t.text.find('\r'), std::string_view::npos) << t.name << " must be LF";
        EXPECT_NE(t.text.back(), ' ') << t.name;
    }
    EXPECT_TRUE(std::is_sorted(names.begin(), names.end()));
    EXPECT_EQ(std::adjacent_find(names.begin(), names.end()), names.end()) << "duplicate template";
}

TEST(EmbeddedTemplates, AllParse) {
    Diagnostics d;
    const TemplateStore store(embedded_templates(), std::nullopt, d);
    EXPECT_TRUE(store.validate(d));
    EXPECT_TRUE(d.empty());
}

TEST(EmbeddedTemplates, FileHeader) {
    Diagnostics d;
    const TemplateStore store(embedded_templates(), std::nullopt, d);
    ASSERT_TRUE(store.has("common/file_header.inja"));

    EXPECT_EQ(render_ok(store, "common/file_header.inja", {{"file_header", ""}, {"source", ""}}),
              "// Generated by mirrortest. Review every TODO(mt).\n");
    EXPECT_EQ(render_ok(store, "common/file_header.inja",
                        {{"file_header", ""}, {"source", "tests/foo_test.cpp"}}),
              "// Generated by mirrortest from tests/foo_test.cpp. Review every TODO(mt).\n");
    EXPECT_EQ(render_ok(store, "common/file_header.inja",
                        {{"file_header", "// Copyright ACME\n// All rights reserved"}, {"source", "t.cpp"}}),
              "// Copyright ACME\n"
              "// All rights reserved\n"
              "// Generated by mirrortest from t.cpp. Review every TODO(mt).\n");
}
