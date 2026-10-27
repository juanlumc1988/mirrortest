// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Plans in, Qt Test files out: the class and its slots, the placeholder as QFAIL
// or QSKIP, the entry point and the include of the file moc writes.

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "mirrortest/backend/qtest_backend.h"
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

ir::TestPlan plan_of(std::vector<ir::TestCase> cases, std::vector<std::string> ns = {}, std::string suite_name = "TestFoo") {
    ir::TestSuite suite;
    suite.name = std::move(suite_name);
    suite.ns = std::move(ns);
    suite.cases = std::move(cases);
    ir::TestPlan plan;
    plan.file = "tst_foo.cpp";
    plan.includes = {{"QtTest", true, {}}, {"foo.h", false, {}}};
    plan.suites.push_back(std::move(suite));
    return plan;
}

struct Built {
    std::vector<backend::QtestViews> views;
    core::Diagnostics                diagnostics;
};

Built build(const std::vector<ir::TestPlan>& plans, backend::QtestOptions options = {}) {
    Built b;
    b.views = backend::build_qtest_views(plans, options, b.diagnostics);
    return b;
}

using Lines = std::vector<std::string>;

Lines strings(const nlohmann::json& array) {
    Lines out;
    for (const auto& l : array) out.push_back(l.get<std::string>());
    return out;
}

const nlohmann::json& first_class(const Built& b) { return b.views.at(0).view["sections"].at(0)["classes"].at(0); }

Lines test_lines(const Built& b, std::size_t test = 0) { return strings(first_class(b)["tests"].at(test)["lines"]); }

struct Rendered {
    core::OutputPlan  plan;
    core::Diagnostics diagnostics;
};

Rendered render(const std::vector<ir::TestPlan>& plans, const backend::QtestOptions& options = {}) {
    Rendered r;
    const core::TemplateStore store(core::embedded_templates(), std::nullopt, r.diagnostics);
    r.plan = backend::generate_qtest(plans, options, store, r.diagnostics);
    return r;
}

// ---- the class and its slots -------------------------------------------------------------------------

TEST(QtestBackendClass, ItIsAQObjectWhoseSlotsAreTheTests) {
    const Built b = build({plan_of({test_case("size", {}), test_case("set_int", {})})});
    EXPECT_EQ(first_class(b)["name"], "TestFoo");
    EXPECT_EQ(strings(first_class(b)["head"]), (Lines{
        "class TestFoo : public QObject {", "    Q_OBJECT", "private slots:", "    void size();", "    void set_int();", "};"}));
}

TEST(QtestBackendClass, EachSlotIsDefinedOutsideTheClass) {
    const Built b = build({plan_of({test_case("size", {stmt(Stmt::Kind::Code, "int x")})})});
    EXPECT_EQ(test_lines(b), (Lines{"void TestFoo::size() {", "    int x;", "}"}));
    EXPECT_EQ(first_class(b)["tests"][0]["name"], "size");
}

TEST(QtestBackendClass, AClassWithoutTestsStillDeclaresItsSection) {
    const Built b = build({plan_of({})});
    EXPECT_EQ(strings(first_class(b)["head"]), (Lines{"class TestFoo : public QObject {", "    Q_OBJECT", "private slots:", "};"}));
    EXPECT_TRUE(first_class(b)["tests"].empty());
}

// ---- statements -------------------------------------------------------------------------------------------

TEST(QtestBackendStatements, EachKindIsSpeltAsAQtTestBodyWouldHaveIt) {
    const Built b = build({plan_of({test_case("size", {
        stmt(Stmt::Kind::Placeholder, "TODO(mt): write the test for Foo::size()"),
        stmt(Stmt::Kind::Blank),
        stmt(Stmt::Kind::Comment, "Arrange"),
        stmt(Stmt::Kind::Code, "Foo sut"),
        stmt(Stmt::Kind::StaticAssert, "noexcept(sut.size())", "Foo::size() is declared noexcept"),
        stmt(Stmt::Kind::Code, "int unused", {}, true),
    })})});
    EXPECT_EQ(test_lines(b), (Lines{
        "void TestFoo::size() {",
        "    QFAIL(\"TODO(mt): write the test for Foo::size()\");",
        "",
        "    // Arrange",
        "    Foo sut;",
        "    static_assert(noexcept(sut.size()), \"Foo::size() is declared noexcept\");",
        "    // int unused;",
        "}"}));
}

TEST(QtestBackendStatements, ASkippedPlaceholderIsQSkip) {
    backend::QtestOptions options;
    options.config.placeholder = core::Placeholder::Skip;
    const Built b = build({plan_of({test_case("t", {stmt(Stmt::Kind::Placeholder, "TODO(mt): x")})})}, options);
    EXPECT_EQ(test_lines(b)[1], "    QSKIP(\"TODO(mt): x\");");
}

TEST(QtestBackendStatements, MessagesAreEscapedSoTheyStayOneStringLiteral) {
    const Built b = build({plan_of({test_case("t", {stmt(Stmt::Kind::Placeholder, "write \"it\" \\ now\nplease")})})});
    EXPECT_EQ(test_lines(b)[1], "    QFAIL(\"write \\\"it\\\" \\\\ now\\nplease\");");
}

// ---- the entry point -------------------------------------------------------------------------------------

TEST(QtestBackendMain, TheThreeEntryPoints) {
    for (const auto& [main, macro] : std::vector<std::pair<core::QtMain, const char*>>{
             {core::QtMain::Appless, "QTEST_APPLESS_MAIN(TestFoo)"},
             {core::QtMain::GuiLess, "QTEST_GUILESS_MAIN(TestFoo)"},
             {core::QtMain::Gui, "QTEST_MAIN(TestFoo)"}}) {
        backend::QtestOptions options;
        options.qt_main = main;
        const Built b = build({plan_of({test_case("t", {})})}, options);
        EXPECT_EQ(b.views.at(0).view["main"][0], macro);
    }
}

TEST(QtestBackendMain, GuiLessIsTheDefault) {
    EXPECT_EQ(build({plan_of({test_case("t", {})})}).views.at(0).view["main"][0], "QTEST_GUILESS_MAIN(TestFoo)");
}

TEST(QtestBackendMain, TheClassIsNamedWithItsNamespaceBecauseTheMacroIsOutsideIt) {
    const Built b = build({plan_of({test_case("t", {})}, {"acme", "net"})});
    EXPECT_EQ(b.views.at(0).view["main"][0], "QTEST_GUILESS_MAIN(acme::net::TestFoo)");
}

TEST(QtestBackendMain, TheMocIncludeFollowsTheNameOfTheFile) {
    ir::TestPlan plan = plan_of({test_case("t", {})});
    EXPECT_EQ(build({plan}).views.at(0).view["main"][2], "#include \"tst_foo.moc\"");
    plan.file = "sub/dir/tst_bar.cpp";
    EXPECT_EQ(build({plan}).views.at(0).view["main"][2], "#include \"tst_bar.moc\"");
    plan.file = "tst_baz.cc";
    EXPECT_EQ(build({plan}).views.at(0).view["main"][2], "#include \"tst_baz.moc\"");
    plan.file = "noextension";
    EXPECT_EQ(build({plan}).views.at(0).view["main"][2], "#include \"noextension.moc\"");
    plan.file = "dir.d/name.with.dots.cpp";
    EXPECT_EQ(build({plan}).views.at(0).view["main"][2], "#include \"name.with.dots.moc\"");
    plan.file = ".hidden";
    EXPECT_EQ(build({plan}).views.at(0).view["main"][2], "#include \".hidden.moc\"");
}

TEST(QtestBackendMain, NothingToTestHasNoEntryPoint) {
    ir::TestPlan plan = plan_of({});
    plan.suites.clear();
    const Built b = build({plan});
    EXPECT_TRUE(b.views.at(0).view["main"].empty());
    EXPECT_TRUE(b.views.at(0).view["sections"].empty());
}

// ---- the view ---------------------------------------------------------------------------------------------

TEST(QtestBackendView, TheViewCarriesThePathTheHeaderTheSourceAndTheIncludes) {
    backend::QtestOptions options;
    options.config.file_header = "// (c) acme";
    options.source = "include/foo.h";
    ir::TestPlan plan = plan_of({test_case("a", {})});
    plan.includes = {{"foo.h", false, {}}, {"utility", true, {}}, {"QtTest", true, {}}, {"utility", true, {}}};
    const Built b = build({plan}, options);
    ASSERT_EQ(b.views.size(), 1u);
    EXPECT_EQ(b.views[0].path, "tst_foo.cpp");
    EXPECT_EQ(b.views[0].view["file_header"], "// (c) acme");
    EXPECT_EQ(b.views[0].view["source"], "include/foo.h");
    EXPECT_EQ(strings(b.views[0].view["includes"]), (Lines{"#include <QtTest>", "#include <utility>", "#include \"foo.h\""}));
}

TEST(QtestBackendView, TheNamespaceOfTheSuiteOpensAndClosesTheSection) {
    const Built b = build({plan_of({test_case("t", {})}, {"acme", "util"})});
    const nlohmann::json& s = b.views[0].view["sections"][0];
    EXPECT_EQ(s["open"], (nlohmann::json{"namespace acme {", "namespace util {"}));
    EXPECT_EQ(s["close"], (nlohmann::json{"}  // namespace util", "}  // namespace acme"}));
}

TEST(QtestBackendView, TheGlobalNamespaceIsASectionWithoutBraces) {
    const Built b = build({plan_of({test_case("t", {})})});
    EXPECT_TRUE(b.views[0].view["sections"][0]["open"].empty());
    EXPECT_TRUE(b.views[0].view["sections"][0]["close"].empty());
}

TEST(QtestBackendView, OneViewPerPlanInOrder) {
    ir::TestPlan a = plan_of({test_case("t", {})});
    ir::TestPlan c = plan_of({test_case("t", {})});
    a.file = "tst_a.cpp";
    c.file = "tst_c.cpp";
    const Built b = build({a, c});
    ASSERT_EQ(b.views.size(), 2u);
    EXPECT_EQ(b.views[0].path, "tst_a.cpp");
    EXPECT_EQ(b.views[1].path, "tst_c.cpp");
    EXPECT_TRUE(build({}).views.empty());
}

TEST(QtestBackendView, SeveralTestClassesShareOneEntryPointWhichGoesToTheFirst) {
    ir::TestPlan plan = plan_of({test_case("a", {})}, {"x"}, "TestA");
    ir::TestSuite second = plan.suites[0];
    second.name = "TestB";
    second.ns = {"y"};
    plan.suites.push_back(second);
    const Built b = build({plan});
    ASSERT_EQ(b.diagnostics.sorted().size(), 1u);
    EXPECT_EQ(b.diagnostics.sorted()[0].code, "mt-unsupported");
    EXPECT_NE(b.diagnostics.sorted()[0].message.find("one entry point"), std::string::npos);
    EXPECT_EQ(b.views[0].view["sections"].size(), 2u);
    EXPECT_EQ(b.views[0].view["main"][0], "QTEST_GUILESS_MAIN(x::TestA)");
}

TEST(QtestBackendView, AFixtureIsReportedAndItsCasesAreWrittenAsPlainSlots) {
    ir::TestPlan plan = plan_of({test_case("t", {})});
    plan.suites[0].fixture = ir::Fixture{};
    const Built b = build({plan});
    ASSERT_EQ(b.diagnostics.sorted().size(), 1u);
    EXPECT_EQ(b.diagnostics.sorted()[0].code, "mt-unsupported");
    EXPECT_EQ(b.diagnostics.sorted()[0].severity, core::Severity::Warning);
    EXPECT_TRUE(build({plan_of({test_case("t", {})})}).diagnostics.sorted().empty());
}

// ---- the file ----------------------------------------------------------------------------------------------

TEST(QtestBackendFile, ALayoutWithANamespaceAndTwoSlots) {
    backend::QtestOptions options;
    options.source = "foo.h";
    const Rendered r = render({plan_of({test_case("a", {stmt(Stmt::Kind::Placeholder, "TODO(mt): a")}),
                                        test_case("b", {stmt(Stmt::Kind::Code, "int x")})},
                                       {"acme"})},
                              options);
    ASSERT_EQ(r.plan.files.size(), 1u);
    EXPECT_EQ(r.plan.files[0].path, "tst_foo.cpp");
    EXPECT_EQ(r.plan.files[0].content,
              "// Generated by mirrortest from foo.h. Review every TODO(mt).\n"
              "\n"
              "#include <QtTest>\n"
              "#include \"foo.h\"\n"
              "\n"
              "namespace acme {\n"
              "\n"
              "class TestFoo : public QObject {\n"
              "    Q_OBJECT\n"
              "private slots:\n"
              "    void a();\n"
              "    void b();\n"
              "};\n"
              "\n"
              "void TestFoo::a() {\n"
              "    QFAIL(\"TODO(mt): a\");\n"
              "}\n"
              "\n"
              "void TestFoo::b() {\n"
              "    int x;\n"
              "}\n"
              "\n"
              "}  // namespace acme\n"
              "\n"
              "QTEST_GUILESS_MAIN(acme::TestFoo)\n"
              "// AUTOMOC: the name of the moc file must follow the name of this file.\n"
              "#include \"tst_foo.moc\"\n");
    EXPECT_TRUE(r.diagnostics.sorted().empty());
}

TEST(QtestBackendFile, TheGlobalNamespaceNeedsNoBracesAndTheFileHeaderComesFirst) {
    backend::QtestOptions options;
    options.config.file_header = "// Copyright acme";
    options.out_dir = "out/tests";
    options.qt_main = core::QtMain::Gui;
    const Rendered r = render({plan_of({test_case("a", {})})}, options);
    ASSERT_EQ(r.plan.files.size(), 1u);
    EXPECT_EQ(r.plan.files[0].path, "out/tests/tst_foo.cpp");
    EXPECT_EQ(r.plan.files[0].content,
              "// Copyright acme\n"
              "// Generated by mirrortest. Review every TODO(mt).\n"
              "\n"
              "#include <QtTest>\n"
              "#include \"foo.h\"\n"
              "\n"
              "class TestFoo : public QObject {\n"
              "    Q_OBJECT\n"
              "private slots:\n"
              "    void a();\n"
              "};\n"
              "\n"
              "void TestFoo::a() {\n"
              "}\n"
              "\n"
              "QTEST_MAIN(TestFoo)\n"
              "// AUTOMOC: the name of the moc file must follow the name of this file.\n"
              "#include \"tst_foo.moc\"\n");
}

TEST(QtestBackendFile, NothingToWriteIsNoFile) {
    EXPECT_TRUE(render({}).plan.files.empty());
}

TEST(QtestBackendFile, ATemplateThatFailsLeavesTheFileOutAndRecordsTheError) {
    core::Diagnostics d;
    const std::vector<core::EmbeddedTemplate> broken = {
        {"common/file_header.inja", "{{ file_header }}"}, {"qtest/test_file.inja", "{{ no_such_variable }}"}};
    const core::TemplateStore store(broken, std::nullopt, d);
    const core::OutputPlan plan = backend::generate_qtest({plan_of({test_case("t", {})})}, {}, store, d);
    EXPECT_TRUE(plan.files.empty());
    EXPECT_TRUE(d.has_errors());
}

// ---- from a header, end to end ---------------------------------------------------------------------------

TEST(QtestBackendFile, EveryGeneratedFileIsCleanText) {
    core::Diagnostics d;
    const ir::Model model = frontend::read_header(
        {"model.h", "#include <QObject>\nnamespace acme { class Model : public QObject { Q_OBJECT\npublic:\n  explicit Model(QObject* parent = nullptr);\n"
                    "  unsigned size() const noexcept;\n  void take(std::string&& s); void bad(Mystery m);\n"
                    "public slots:\n  void apply();\nsignals:\n  void changed(int value);\n}; }"},
        "acme/model.h", {}, d);
    backend::PlannerOptions po;
    po.framework = ir::Framework::QTest;
    po.header_include = "acme/model.h";
    const std::vector<ir::TestPlan> plans = backend::plan_tests(model, po, d);
    ASSERT_EQ(plans.size(), 1u);
    EXPECT_EQ(plans[0].file, "tst_model.cpp");

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
    EXPECT_NE(text.find("QSignalSpy spy(&sut, &Model::changed);"), std::string::npos) << text;
    EXPECT_EQ(text, render(plans).plan.files[0].content) << "the same plan is the same text";
}

}  // namespace

// ---- cases over a table ------------------------------------------------------------------------------------------

namespace {

ir::TestCase table_case(std::string name, std::vector<std::pair<const char*, const char*>> columns) {
    ir::TestCase c = test_case(name, {stmt(Stmt::Kind::Placeholder, "TODO(mt): " + name), stmt(Stmt::Kind::Blank),
                                      stmt(Stmt::Kind::Comment, "Arrange"), stmt(Stmt::Kind::Data), stmt(Stmt::Kind::Comment, "Act")});
    ir::DataTable table;
    for (const auto& k : columns) table.columns.push_back({ir::type_from_spelling(k.first), k.second});
    c.data = table;
    return c;
}

}  // namespace

TEST(QtestBackendTable, ADataSlotBeforeTheSlotAndQFetchInIt) {
    const Built b = build({plan_of({table_case("set", {{"int", "n"}, {"const char*", "unit"}}), test_case("plain", {})})});
    EXPECT_EQ(strings(first_class(b)["head"]), (Lines{
        "class TestFoo : public QObject {", "    Q_OBJECT", "private slots:", "    void set_data();", "    void set();", "    void plain();", "};"}));
    ASSERT_EQ(first_class(b)["tests"].size(), 3u);
    EXPECT_EQ(test_lines(b, 0), (Lines{
        "void TestFoo::set_data() {",
        "    QTest::addColumn<int>(\"n\");",
        "    QTest::addColumn<const char*>(\"unit\");",
        "",
        "    // TODO(mt): one row for each case to run",
        "    int n{};",
        "    const char* unit{};",
        "    QTest::newRow(\"default\") << n << unit;",
        "}"}));
    EXPECT_EQ(test_lines(b, 1), (Lines{
        "void TestFoo::set() {",
        "    QFAIL(\"TODO(mt): set\");",
        "",
        "    // Arrange",
        "    QFETCH(int, n);",
        "    QFETCH(const char*, unit);",
        "    // Act",
        "}"}));
    EXPECT_EQ(test_lines(b, 2), (Lines{"void TestFoo::plain() {", "}"}));
}

TEST(QtestBackendTable, TheDataFunctionIsNamedAfterTheCase) {
    const Built b = build({plan_of({table_case("set_int", {{"int", "n"}})})});
    EXPECT_EQ(first_class(b)["tests"][0]["name"], "set_int_data");
    EXPECT_EQ(first_class(b)["tests"][1]["name"], "set_int");
}

TEST(QtestBackendTable, TheWholeFile) {
    backend::QtestOptions options;
    options.source = "foo.h";
    const Rendered r = render({plan_of({table_case("set", {{"int", "n"}})})}, options);
    ASSERT_EQ(r.plan.files.size(), 1u);
    EXPECT_NE(r.plan.files[0].content.find(
                  "private slots:\n    void set_data();\n    void set();\n};\n\n"
                  "void TestFoo::set_data() {\n"),
              std::string::npos)
        << r.plan.files[0].content;
}

// ---- the conditions a test is under (plan 9.6) ----------------------------------------------------------------------

TEST(QtestBackendGuards, ASlotAndItsDefinitionAreUnderTheConditionsOfTheTest) {
    ir::TestCase guarded = test_case("transfer", {stmt(Stmt::Kind::Code, "int x")});
    guarded.guards = {"defined(DMA)", "!(defined(PIO))"};
    const Built b = build({plan_of({test_case("open", {}), guarded})});
    EXPECT_EQ(strings(first_class(b)["head"]), (Lines{"class TestFoo : public QObject {", "    Q_OBJECT", "private slots:", "    void open();",
                                                      "#if defined(DMA)", "#if !(defined(PIO))", "    void transfer();", "#endif", "#endif", "};"}));
    EXPECT_EQ(test_lines(b, 1), (Lines{"#if defined(DMA)", "#if !(defined(PIO))", "void TestFoo::transfer() {", "    int x;", "}", "#endif", "#endif"}));
}

TEST(QtestBackendGuards, AClassUnderAConditionHasItsHeadItsDefinitionsAndItsMainUnderIt) {
    ir::TestPlan plan = plan_of({test_case("a", {})});
    plan.suites[0].guards = {"defined(EXTRA)"};
    const Built b = build({plan});
    const Lines head = strings(first_class(b)["head"]);
    EXPECT_EQ(head.front(), "#if defined(EXTRA)");
    EXPECT_EQ(head.back(), "#endif");
    EXPECT_EQ(test_lines(b, 0).front(), "#if defined(EXTRA)");
    const Lines main = strings(b.views.at(0).view["main"]);
    ASSERT_GE(main.size(), 3u);
    EXPECT_EQ(main.front(), "#if defined(EXTRA)");
    EXPECT_EQ(main.back(), "#endif");
}

TEST(QtestBackendGuards, ACaseThatRunsOverATableHasItsDataSlotUnderTheSameConditions) {
    ir::TestCase guarded = test_case("transfer", {});
    guarded.guards = {"defined(DMA)"};
    guarded.data = ir::DataTable{};
    guarded.data->columns.push_back({ir::make_type("int"), "size"});
    const Built b = build({plan_of({guarded})});
    EXPECT_EQ(strings(first_class(b)["head"]), (Lines{"class TestFoo : public QObject {", "    Q_OBJECT", "private slots:", "#if defined(DMA)",
                                                      "    void transfer_data();", "    void transfer();", "#endif", "};"}));
    EXPECT_EQ(test_lines(b, 0).front(), "#if defined(DMA)");
}

