// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Plans in, GoogleTest files out: how each kind of statement is spelt, what the
// placeholder becomes, how the file is laid out.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "mirrortest/backend/gtest_backend.h"
#include "mirrortest/backend/test_planner.h"
#include "mirrortest/frontend/code_frontend.h"

using namespace mt;
using ir::Stmt;

namespace {

Stmt stmt(Stmt::Kind kind, std::string text = {}, std::string message = {}, bool commented_out = false) {
    Stmt s;
    s.kind = kind;
    s.text = std::move(text);
    s.message = std::move(message);
    s.commented_out = commented_out;
    return s;
}

ir::TestCase test_case(std::string name, std::vector<Stmt> body) {
    ir::TestCase c;
    c.name = std::move(name);
    c.body = std::move(body);
    return c;
}

ir::TestPlan plan_of(std::vector<ir::TestCase> cases, std::vector<std::string> ns = {}, std::string suite_name = "FooTest") {
    ir::TestSuite suite;
    suite.name = std::move(suite_name);
    suite.ns = std::move(ns);
    suite.cases = std::move(cases);
    ir::TestPlan plan;
    plan.file = "foo_test.cpp";
    plan.includes = {{"gtest/gtest.h", true, {}}, {"foo.h", false, {}}};
    plan.suites.push_back(std::move(suite));
    return plan;
}

struct Built {
    std::vector<backend::GtestViews> views;
};

Built build(const std::vector<ir::TestPlan>& plans, backend::GtestOptions options = {}) {
    Built b;
    b.views = backend::build_gtest_views(plans, options);
    return b;
}

std::vector<std::string> lines_of(const nlohmann::json& test) {
    std::vector<std::string> out;
    for (const auto& l : test["lines"]) out.push_back(l.get<std::string>());
    return out;
}

std::vector<std::string> body_lines(const Built& b, std::size_t test = 0) {
    return lines_of(b.views.at(0).view["sections"].at(0)["tests"].at(test));
}

struct Rendered {
    core::OutputPlan plan;
    core::Diagnostics diagnostics;
};

Rendered render(const std::vector<ir::TestPlan>& plans, const backend::GtestOptions& options = {}) {
    Rendered r;
    const core::TemplateStore store(core::embedded_templates(), std::nullopt, r.diagnostics);
    r.plan = backend::generate_gtest(plans, options, store, r.diagnostics);
    return r;
}

using Lines = std::vector<std::string>;

// ---- statements -----------------------------------------------------------------------------------------------

TEST(GtestBackendStatements, EachKindIsSpeltAsAGoogleTestBodyWouldHaveIt) {
    const Built b = build({plan_of({test_case("size", {
        stmt(Stmt::Kind::Placeholder, "TODO(mt): write the test for Foo::size()"),
        stmt(Stmt::Kind::Blank),
        stmt(Stmt::Kind::Comment, "Arrange"),
        stmt(Stmt::Kind::Code, "Foo sut"),
        stmt(Stmt::Kind::StaticAssert, "noexcept(sut.size())", "Foo::size() is declared noexcept"),
    })})});
    EXPECT_EQ(body_lines(b), (Lines{
        "TEST(FooTest, size) {",
        "    FAIL() << \"TODO(mt): write the test for Foo::size()\";",
        "",
        "    // Arrange",
        "    Foo sut;",
        "    static_assert(noexcept(sut.size()), \"Foo::size() is declared noexcept\");",
        "}"}));
}

TEST(GtestBackendStatements, ASkippedPlaceholderIsGtestSkip) {
    backend::GtestOptions options;
    options.config.placeholder = core::Placeholder::Skip;
    const Built b = build({plan_of({test_case("t", {stmt(Stmt::Kind::Placeholder, "TODO(mt): x")})})}, options);
    EXPECT_EQ(body_lines(b)[1], "    GTEST_SKIP() << \"TODO(mt): x\";");
}

TEST(GtestBackendStatements, CommentedOutStatementsAreComments) {
    const Built b = build({plan_of({test_case("t", {
        stmt(Stmt::Kind::Code, "const int result = sut.f(m)", {}, true),
        stmt(Stmt::Kind::StaticAssert, "noexcept(sut.f(m))", "declared noexcept", true),
    })})});
    EXPECT_EQ(body_lines(b), (Lines{
        "TEST(FooTest, t) {",
        "    // const int result = sut.f(m);",
        "    // static_assert(noexcept(sut.f(m)), \"declared noexcept\");",
        "}"}));
}

TEST(GtestBackendStatements, MessagesAreEscapedSoTheyStayOneStringLiteral) {
    const Built b = build({plan_of({test_case("t", {
        stmt(Stmt::Kind::Placeholder, "write \"it\" \\ now\nplease"),
        stmt(Stmt::Kind::StaticAssert, "true", "say \"x\""),
    })})});
    EXPECT_EQ(body_lines(b)[1], "    FAIL() << \"write \\\"it\\\" \\\\ now\\nplease\";");
    EXPECT_EQ(body_lines(b)[2], "    static_assert(true, \"say \\\"x\\\"\");");
}

TEST(GtestBackendStatements, ABlankLineHasNoSpaces) {
    const Built b = build({plan_of({test_case("t", {stmt(Stmt::Kind::Blank)})})});
    EXPECT_EQ(body_lines(b)[1], "");
}

TEST(GtestBackendStatements, ATestWithoutAnyStatementIsAnEmptyBody) {
    const Built b = build({plan_of({test_case("t", {})})});
    EXPECT_EQ(body_lines(b), (Lines{"TEST(FooTest, t) {", "}"}));
}

// ---- the view -----------------------------------------------------------------------------------------------------

TEST(GtestBackendView, TheViewCarriesThePathTheHeaderTheSourceAndTheTests) {
    backend::GtestOptions options;
    options.config.file_header = "// (c) acme";
    options.source = "include/foo.h";
    const Built b = build({plan_of({test_case("a", {}), test_case("b", {})})}, options);
    ASSERT_EQ(b.views.size(), 1u);
    EXPECT_EQ(b.views[0].path, "foo_test.cpp");
    const nlohmann::json& v = b.views[0].view;
    EXPECT_EQ(v["file_header"], "// (c) acme");
    EXPECT_EQ(v["source"], "include/foo.h");
    ASSERT_EQ(v["sections"].size(), 1u);
    ASSERT_EQ(v["sections"][0]["tests"].size(), 2u);
    EXPECT_EQ(v["sections"][0]["tests"][0]["suite"], "FooTest");
    EXPECT_EQ(v["sections"][0]["tests"][0]["name"], "a");
    EXPECT_EQ(v["sections"][0]["tests"][1]["name"], "b");
}

TEST(GtestBackendView, SystemIncludesComeFirstSortedAndEachOnlyOnce) {
    ir::TestPlan plan = plan_of({test_case("t", {})});
    plan.includes = {{"foo.h", false, {}}, {"utility", true, {}}, {"gtest/gtest.h", true, {}}, {"utility", true, {}}, {"acme/bar.h", false, {}}};
    const Built b = build({plan});
    const nlohmann::json& inc = b.views[0].view["includes"];
    ASSERT_EQ(inc.size(), 4u);
    EXPECT_EQ(inc[0], "#include <gtest/gtest.h>");
    EXPECT_EQ(inc[1], "#include <utility>");
    EXPECT_EQ(inc[2], "#include \"acme/bar.h\"");
    EXPECT_EQ(inc[3], "#include \"foo.h\"");
}

TEST(GtestBackendView, TheNamespaceOfTheSuiteOpensAndClosesTheSection) {
    const Built b = build({plan_of({test_case("t", {})}, {"acme", "util"})});
    const nlohmann::json& s = b.views[0].view["sections"][0];
    EXPECT_EQ(s["open"], (nlohmann::json{"namespace acme {", "namespace util {"}));
    EXPECT_EQ(s["close"], (nlohmann::json{"}  // namespace util", "}  // namespace acme"}));
}

TEST(GtestBackendView, TheGlobalNamespaceIsASectionWithoutBraces) {
    const Built b = build({plan_of({test_case("t", {})})});
    const nlohmann::json& s = b.views[0].view["sections"][0];
    EXPECT_TRUE(s["open"].empty());
    EXPECT_TRUE(s["close"].empty());
}

TEST(GtestBackendView, SuitesOfTheSameNamespaceShareASectionInOrderOfFirstAppearance) {
    ir::TestPlan plan = plan_of({test_case("a", {})}, {"x"}, "ATest");
    ir::TestSuite second = plan.suites[0];
    second.name = "BTest";
    second.ns = {"y"};
    second.cases = {test_case("b", {})};
    ir::TestSuite third = plan.suites[0];
    third.name = "CTest";
    third.cases = {test_case("c", {})};
    plan.suites.push_back(second);
    plan.suites.push_back(third);

    const Built b = build({plan});
    const nlohmann::json& sections = b.views[0].view["sections"];
    ASSERT_EQ(sections.size(), 2u);
    EXPECT_EQ(sections[0]["open"][0], "namespace x {");
    ASSERT_EQ(sections[0]["tests"].size(), 2u);
    EXPECT_EQ(sections[0]["tests"][0]["suite"], "ATest");
    EXPECT_EQ(sections[0]["tests"][1]["suite"], "CTest");
    EXPECT_EQ(sections[1]["open"][0], "namespace y {");
    EXPECT_EQ(sections[1]["tests"][0]["suite"], "BTest");
}

TEST(GtestBackendView, OneViewPerPlanInOrder) {
    ir::TestPlan a = plan_of({test_case("t", {})});
    ir::TestPlan c = plan_of({test_case("t", {})});
    a.file = "a_test.cpp";
    c.file = "c_test.cpp";
    const Built b = build({a, c});
    ASSERT_EQ(b.views.size(), 2u);
    EXPECT_EQ(b.views[0].path, "a_test.cpp");
    EXPECT_EQ(b.views[1].path, "c_test.cpp");
    EXPECT_TRUE(build({}).views.empty());
}

// ---- fixtures -----------------------------------------------------------------------------------------------

ir::Fixture fixture_of(std::string name) {
    ir::Fixture f;
    f.name = std::move(name);
    return f;
}

Lines fixture_lines(const Built& b, std::size_t fixture = 0) {
    Lines out;
    for (const auto& l : b.views.at(0).view["sections"].at(0)["fixtures"].at(fixture)["lines"]) out.push_back(l.get<std::string>());
    return out;
}

TEST(GtestBackendFixture, ItIsAClassAndItsTestsAreTestF) {
    ir::TestPlan plan = plan_of({test_case("size", {stmt(Stmt::Kind::Code, "int x")})});
    ir::Fixture f = fixture_of("FooTest");
    f.members = {stmt(Stmt::Kind::Code, "int initial{}"), stmt(Stmt::Kind::Code, "Foo sut")};
    f.initialisers = {"sut(initial)"};
    f.set_up = {stmt(Stmt::Kind::Comment, "TODO(mt): bring sut to the state every test starts from")};
    plan.suites[0].fixture = f;
    const Built b = build({plan});
    EXPECT_EQ(fixture_lines(b), (Lines{
        "class FooTest : public ::testing::Test {",
        "protected:",
        "    FooTest() : sut(initial) {}",
        "",
        "    void SetUp() override {",
        "        // TODO(mt): bring sut to the state every test starts from",
        "    }",
        "",
        "    int initial{};",
        "    Foo sut;",
        "};"}));
    EXPECT_EQ(body_lines(b)[0], "TEST_F(FooTest, size) {");
}

TEST(GtestBackendFixture, EachPartIsThereOnlyIfItHasSomethingToSay) {
    ir::TestPlan plan = plan_of({test_case("t", {})});
    ir::Fixture f = fixture_of("FooTest");
    f.members = {stmt(Stmt::Kind::Code, "Foo sut")};
    plan.suites[0].fixture = f;
    EXPECT_EQ(fixture_lines(build({plan})), (Lines{"class FooTest : public ::testing::Test {", "protected:", "    Foo sut;", "};"}));

    f.tear_down = {stmt(Stmt::Kind::Code, "sut.stop()")};
    plan.suites[0].fixture = f;
    EXPECT_EQ(fixture_lines(build({plan})), (Lines{
        "class FooTest : public ::testing::Test {", "protected:", "    void TearDown() override {", "        sut.stop();", "    }", "",
        "    Foo sut;", "};"}));

    f.members.clear();
    f.tear_down.clear();
    plan.suites[0].fixture = f;
    EXPECT_EQ(fixture_lines(build({plan})), (Lines{"class FooTest : public ::testing::Test {", "protected:", "};"}));
}

TEST(GtestBackendFixture, SeveralInitialisersAreOneList) {
    ir::TestPlan plan = plan_of({test_case("t", {})});
    ir::Fixture f = fixture_of("FooTest");
    f.initialisers = {"a(1)", "sut(a)"};
    plan.suites[0].fixture = f;
    EXPECT_EQ(fixture_lines(build({plan}))[2], "    FooTest() : a(1), sut(a) {}");
}

TEST(GtestBackendFixture, AMemberThatCouldNotBeMadeIsAComment) {
    ir::TestPlan plan = plan_of({test_case("t", {})});
    ir::Fixture f = fixture_of("FooTest");
    f.members = {stmt(Stmt::Kind::Comment, "TODO(mt): 'Foo' is abstract"), stmt(Stmt::Kind::Code, "Foo sut", {}, true)};
    plan.suites[0].fixture = f;
    EXPECT_EQ(fixture_lines(build({plan})), (Lines{
        "class FooTest : public ::testing::Test {", "protected:", "    // TODO(mt): 'Foo' is abstract", "    // Foo sut;", "};"}));
}

TEST(GtestBackendFixture, ItSitsInTheNamespaceOfItsSuiteBeforeTheTests) {
    ir::TestPlan plan = plan_of({test_case("t", {})}, {"acme"});
    ir::Fixture f = fixture_of("FooTest");
    f.members = {stmt(Stmt::Kind::Code, "Foo sut")};
    plan.suites[0].fixture = f;
    const Built b = build({plan});
    ASSERT_EQ(b.views.at(0).view["sections"].size(), 1u);
    EXPECT_EQ(b.views.at(0).view["sections"][0]["fixtures"].size(), 1u);
    EXPECT_EQ(b.views.at(0).view["sections"][0]["open"][0], "namespace acme {");
}

TEST(GtestBackendFixture, ASuiteWithoutOneHasNone) {
    const Built b = build({plan_of({test_case("t", {})})});
    EXPECT_TRUE(b.views.at(0).view["sections"][0]["fixtures"].empty());
    EXPECT_EQ(body_lines(b)[0], "TEST(FooTest, t) {");
}

TEST(GtestBackendFixture, TheFileHasTheClassFirstThenTheTests) {
    ir::TestPlan plan = plan_of({test_case("a", {stmt(Stmt::Kind::Placeholder, "TODO(mt): a")}), test_case("b", {})}, {"acme"});
    ir::Fixture f = fixture_of("FooTest");
    f.members = {stmt(Stmt::Kind::Code, "Foo sut")};
    f.set_up = {stmt(Stmt::Kind::Comment, "TODO(mt): set up")};
    plan.suites[0].fixture = f;
    backend::GtestOptions options;
    options.source = "foo.h";
    const Rendered r = render({plan}, options);
    ASSERT_EQ(r.plan.files.size(), 1u);
    EXPECT_EQ(r.plan.files[0].content,
              "// Generated by mirrortest from foo.h. Review every TODO(mt).\n"
              "\n"
              "#include <gtest/gtest.h>\n"
              "#include \"foo.h\"\n"
              "\n"
              "namespace acme {\n"
              "\n"
              "class FooTest : public ::testing::Test {\n"
              "protected:\n"
              "    void SetUp() override {\n"
              "        // TODO(mt): set up\n"
              "    }\n"
              "\n"
              "    Foo sut;\n"
              "};\n"
              "\n"
              "TEST_F(FooTest, a) {\n"
              "    FAIL() << \"TODO(mt): a\";\n"
              "}\n"
              "\n"
              "TEST_F(FooTest, b) {\n"
              "}\n"
              "\n"
              "}  // namespace acme\n");
}

// ---- the file ------------------------------------------------------------------------------------------------------

TEST(GtestBackendFile, ALayoutWithANamespaceAndTwoTests) {
    backend::GtestOptions options;
    options.source = "foo.h";
    const Rendered r = render({plan_of({test_case("a", {stmt(Stmt::Kind::Placeholder, "TODO(mt): a")}),
                                        test_case("b", {stmt(Stmt::Kind::Code, "int x")})},
                                       {"acme"})},
                              options);
    ASSERT_EQ(r.plan.files.size(), 1u);
    EXPECT_EQ(r.plan.files[0].path, "foo_test.cpp");
    EXPECT_EQ(r.plan.files[0].content,
              "// Generated by mirrortest from foo.h. Review every TODO(mt).\n"
              "\n"
              "#include <gtest/gtest.h>\n"
              "#include \"foo.h\"\n"
              "\n"
              "namespace acme {\n"
              "\n"
              "TEST(FooTest, a) {\n"
              "    FAIL() << \"TODO(mt): a\";\n"
              "}\n"
              "\n"
              "TEST(FooTest, b) {\n"
              "    int x;\n"
              "}\n"
              "\n"
              "}  // namespace acme\n");
    EXPECT_TRUE(r.diagnostics.sorted().empty());
}

TEST(GtestBackendFile, TheGlobalNamespaceNeedsNoBracesAndTheFileHeaderComesFirst) {
    backend::GtestOptions options;
    options.config.file_header = "// Copyright acme";
    options.out_dir = "out/tests";
    const Rendered r = render({plan_of({test_case("a", {})})}, options);
    ASSERT_EQ(r.plan.files.size(), 1u);
    EXPECT_EQ(r.plan.files[0].path, "out/tests/foo_test.cpp");
    EXPECT_EQ(r.plan.files[0].content,
              "// Copyright acme\n"
              "// Generated by mirrortest. Review every TODO(mt).\n"
              "\n"
              "#include <gtest/gtest.h>\n"
              "#include \"foo.h\"\n"
              "\n"
              "TEST(FooTest, a) {\n"
              "}\n");
}

TEST(GtestBackendFile, NothingToWriteIsNoFile) {
    EXPECT_TRUE(render({}).plan.files.empty());
}

TEST(GtestBackendFile, ATemplateThatFailsLeavesTheFileOutAndRecordsTheError) {
    core::Diagnostics d;
    const std::vector<core::EmbeddedTemplate> broken = {
        {"common/file_header.inja", "{{ file_header }}"}, {"gtest/test_file.inja", "{{ no_such_variable }}"}};
    const core::TemplateStore store(broken, std::nullopt, d);
    const core::OutputPlan plan = backend::generate_gtest({plan_of({test_case("t", {})})}, {}, store, d);
    EXPECT_TRUE(plan.files.empty());
    EXPECT_TRUE(d.has_errors());
}

// ---- from a header, end to end ---------------------------------------------------------------------------------

TEST(GtestBackendFile, EveryGeneratedFileIsCleanText) {
    core::Diagnostics d;
    const ir::Model model = frontend::read_header(
        {"foo.h", "namespace acme { class Foo { public: Foo(); explicit Foo(int n); ~Foo(); unsigned size() const noexcept;\n"
                  "  void take(std::string&& s); void bad(Mystery m); bool operator==(const Foo&) const; }; }"},
        "acme/foo.h", {}, d);
    backend::PlannerOptions po;
    po.header_include = "acme/foo.h";
    const std::vector<ir::TestPlan> plans = backend::plan_tests(model, po, d);
    ASSERT_EQ(plans.size(), 1u);

    const Rendered r = render(plans);
    ASSERT_EQ(r.plan.files.size(), 1u);
    const std::string& text = r.plan.files[0].content;
    EXPECT_EQ(text.find('\r'), std::string::npos);
    EXPECT_EQ(text.find('\t'), std::string::npos);
    EXPECT_EQ(text.find(" \n"), std::string::npos) << "no trailing whitespace";
    EXPECT_EQ(text.find("\n\n\n"), std::string::npos) << "no double blank line";
    ASSERT_FALSE(text.empty());
    EXPECT_EQ(text.back(), '\n');
    EXPECT_NE(text[text.size() - 2], '\n') << "one newline at the end";
    EXPECT_EQ(text, render(plans).plan.files[0].content) << "the same plan is the same text";
}

}  // namespace
