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
#include "mirrortest/ir/type.h"

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

TEST(GtestBackendFixture, NoBlankLineBeforeTheEndWhenThereAreNoMembers) {
    // The blank line separates the methods from the members: no members, nothing to separate.
    ir::TestPlan plan = plan_of({test_case("t", {})});
    ir::Fixture f = fixture_of("FooTest");
    f.set_up = {stmt(Stmt::Kind::Code, "a()")};
    plan.suites[0].fixture = f;
    EXPECT_EQ(fixture_lines(build({plan})), (Lines{
        "class FooTest : public ::testing::Test {", "protected:", "    void SetUp() override {", "        a();", "    }", "};"}));
}

TEST(GtestBackendFixture, ABlankLineInAMethodHasNoIndentation) {
    ir::TestPlan plan = plan_of({test_case("t", {})});
    ir::Fixture f = fixture_of("FooTest");
    f.set_up = {stmt(Stmt::Kind::Code, "a()"), stmt(Stmt::Kind::Blank), stmt(Stmt::Kind::Code, "b()")};
    plan.suites[0].fixture = f;
    EXPECT_EQ(fixture_lines(build({plan})), (Lines{
        "class FooTest : public ::testing::Test {", "protected:", "    void SetUp() override {", "        a();", "", "        b();", "    }",
        "};"}));
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

// ---- mocks ---------------------------------------------------------------------------------------------------------

namespace {

ir::Type type(const char* spelling) { return ir::type_from_spelling(spelling); }

ir::MockMethod mock_method(const char* name, const char* ret, std::vector<const char*> params = {}) {
    ir::MockMethod m;
    m.name = name;
    m.ret = type(ret);
    for (const char* p : params) m.params.push_back(type(p));
    return m;
}

ir::MockClass mock_of(std::vector<ir::MockMethod> methods, std::vector<std::string> ns = {}) {
    ir::MockClass mock;
    mock.file = "mock_sensor.h";
    mock.name = "MockSensor";
    mock.base = "Sensor";
    mock.ns = std::move(ns);
    mock.header = "sensor.h";
    mock.methods = std::move(methods);
    return mock;
}

struct RenderedMocks {
    core::OutputPlan  plan;
    core::Diagnostics diagnostics;
};

RenderedMocks render_mocks(const std::vector<ir::MockClass>& mocks, const backend::GtestOptions& options = {}) {
    RenderedMocks r;
    const core::TemplateStore store(core::embedded_templates(), std::nullopt, r.diagnostics);
    r.plan = backend::generate_gtest_mocks(mocks, options, store, r.diagnostics);
    return r;
}

std::vector<std::string> mock_lines(const ir::MockClass& mock) {
    const std::vector<backend::GtestMockViews> views = backend::build_gtest_mock_views({mock}, {});
    std::vector<std::string> out;
    for (const auto& l : views.at(0).view["lines"]) out.push_back(l.get<std::string>());
    return out;
}

}  // namespace

TEST(GtestBackendMocks, TheFileOfAMock) {
    backend::GtestOptions options;
    options.source = "sensor.h";
    const RenderedMocks r = render_mocks({mock_of({mock_method("read", "int"), mock_method("set", "void", {"double", "const std::string&"})})}, options);
    ASSERT_EQ(r.plan.files.size(), 1u);
    EXPECT_EQ(r.plan.files[0].path, "mock_sensor.h");
    EXPECT_EQ(r.plan.files[0].content,
              "// Generated by mirrortest from sensor.h. Review every TODO(mt).\n"
              "#ifndef MOCK_SENSOR_H\n"
              "#define MOCK_SENSOR_H\n"
              "\n"
              "#include <gmock/gmock.h>\n"
              "#include \"sensor.h\"\n"
              "\n"
              "class MockSensor : public Sensor {\n"
              "public:\n"
              "    MOCK_METHOD(int, read, (), (override));\n"
              "    MOCK_METHOD(void, set, (double, const std::string&), (override));\n"
              "};\n"
              "\n"
              "#endif  // MOCK_SENSOR_H\n");
    EXPECT_TRUE(r.diagnostics.sorted().empty());
}

TEST(GtestBackendMocks, InANamespaceAndWithPragmaOnce) {
    backend::GtestOptions options;
    options.config.guard = core::GuardStyle::PragmaOnce;
    options.config.file_header = "// Copyright acme";
    options.out_dir = "out/mocks";
    const RenderedMocks r = render_mocks({mock_of({mock_method("read", "int")}, {"acme", "hw"})}, options);
    ASSERT_EQ(r.plan.files.size(), 1u);
    EXPECT_EQ(r.plan.files[0].path, "out/mocks/mock_sensor.h");
    EXPECT_EQ(r.plan.files[0].content,
              "// Copyright acme\n"
              "// Generated by mirrortest. Review every TODO(mt).\n"
              "#pragma once\n"
              "\n"
              "#include <gmock/gmock.h>\n"
              "#include \"sensor.h\"\n"
              "\n"
              "namespace acme {\n"
              "namespace hw {\n"
              "\n"
              "class MockSensor : public Sensor {\n"
              "public:\n"
              "    MOCK_METHOD(int, read, (), (override));\n"
              "};\n"
              "\n"
              "}  // namespace hw\n"
              "}  // namespace acme\n");
}

TEST(GtestBackendMocks, ThePathOfTheMockMakesTheGuard) {
    ir::MockClass mock = mock_of({mock_method("read", "int")});
    mock.file = "mocks/sensor-mock.h";
    const auto views = backend::build_gtest_mock_views({mock}, {});
    ASSERT_EQ(views.size(), 1u);
    EXPECT_EQ(views[0].path, "mocks/sensor-mock.h");
    EXPECT_EQ(views[0].view["guard"], "MOCKS_SENSOR_MOCK_H");
}

TEST(GtestBackendMocks, ATypeWithACommaIsInParentheses) {
    const ir::MockClass mock = mock_of({
        mock_method("table", "std::map<int, std::string>", {"const std::map<int, int>&", "int"}),
        mock_method("pair", "std::pair<int, int>"),
        mock_method("plain", "std::vector<int>", {"std::vector<int>"}),
        mock_method("callback", "void", {"void(*)(int, int)"}),
    });
    EXPECT_EQ(mock_lines(mock), (Lines{
        "class MockSensor : public Sensor {",
        "public:",
        "    MOCK_METHOD((std::map<int, std::string>), table, ((const std::map<int, int>&), int), (override));",
        "    MOCK_METHOD((std::pair<int, int>), pair, (), (override));",
        "    MOCK_METHOD(std::vector<int>, plain, (std::vector<int>), (override));",
        "    MOCK_METHOD(void, callback, ((void(*)(int, int))), (override));",
        "};"}));
}

TEST(GtestBackendMocks, TheSpecifiersAreConstReferenceNoexceptOverrideInThatOrder) {
    ir::MockMethod all = mock_method("a", "int");
    all.is_const = true;
    all.is_noexcept = true;
    all.ref_qualifier = ir::RefKind::LValue;
    ir::MockMethod rvalue = mock_method("b", "void");
    rvalue.ref_qualifier = ir::RefKind::RValue;
    ir::MockMethod only_const = mock_method("c", "int");
    only_const.is_const = true;
    ir::MockMethod only_noexcept = mock_method("d", "int");
    only_noexcept.is_noexcept = true;
    EXPECT_EQ(mock_lines(mock_of({all, rvalue, only_const, only_noexcept})), (Lines{
        "class MockSensor : public Sensor {",
        "public:",
        "    MOCK_METHOD(int, a, (), (const, ref(&), noexcept, override));",
        "    MOCK_METHOD(void, b, (), (ref(&&), override));",
        "    MOCK_METHOD(int, c, (), (const, override));",
        "    MOCK_METHOD(int, d, (), (noexcept, override));",
        "};"}));
}

TEST(GtestBackendMocks, NotesAreCommentsAboveTheClass) {
    ir::MockClass mock = mock_of({mock_method("read", "int")});
    mock.notes = {"'Sensor::operator==' cannot be mocked", "the base 'X' is elsewhere"};
    EXPECT_EQ(mock_lines(mock), (Lines{
        "// TODO(mt): 'Sensor::operator==' cannot be mocked",
        "// TODO(mt): the base 'X' is elsewhere",
        "class MockSensor : public Sensor {",
        "public:",
        "    MOCK_METHOD(int, read, (), (override));",
        "};"}));
}

TEST(GtestBackendMocks, OneFilePerMockInOrder) {
    ir::MockClass a = mock_of({mock_method("a", "int")});
    ir::MockClass b = mock_of({mock_method("b", "int")});
    a.file = "mock_a.h";
    b.file = "mock_b.h";
    const RenderedMocks r = render_mocks({a, b});
    ASSERT_EQ(r.plan.files.size(), 2u);
    EXPECT_EQ(r.plan.files[0].path, "mock_a.h");
    EXPECT_EQ(r.plan.files[1].path, "mock_b.h");
    EXPECT_TRUE(render_mocks({}).plan.files.empty());
}

TEST(GtestBackendMocks, ThePlannersMocksRenderAsWrittenByHand) {
    // The two halves together, on a header with a type that has a comma.
    core::Diagnostics d;
    const ir::Model model = frontend::read_header(
        {"sensor.h",
         "namespace acme {\n"
         "class Sensor {\n"
         "public:\n"
         "    virtual ~Sensor() = default;\n"
         "    virtual std::map<int, int> table() const = 0;\n"
         "};\n"
         "}\n"},
        "acme/sensor.h", {}, d);
    backend::PlannerOptions options;
    options.config.gtest.mocks = true;
    options.header_include = "acme/sensor.h";
    const RenderedMocks r = render_mocks(backend::plan_mocks(model, options, d));
    ASSERT_EQ(r.plan.files.size(), 1u);
    EXPECT_EQ(r.plan.files[0].content,
              "// Generated by mirrortest. Review every TODO(mt).\n"
              "#ifndef MOCK_SENSOR_H\n"
              "#define MOCK_SENSOR_H\n"
              "\n"
              "#include <gmock/gmock.h>\n"
              "#include \"acme/sensor.h\"\n"
              "\n"
              "namespace acme {\n"
              "\n"
              "class MockSensor : public Sensor {\n"
              "public:\n"
              "    MOCK_METHOD((std::map<int, int>), table, (), (const, override));\n"
              "};\n"
              "\n"
              "}  // namespace acme\n"
              "\n"
              "#endif  // MOCK_SENSOR_H\n");
}
