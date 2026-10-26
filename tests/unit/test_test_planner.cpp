// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Model in, test plans out: which cases a class gets, what the body of each
// does, and where the skeleton gives up and leaves a comment.

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "mirrortest/backend/template_expand.h"
#include "mirrortest/backend/test_planner.h"
#include "mirrortest/core/naming.h"
#include "mirrortest/frontend/code_frontend.h"

using namespace mt;
using ir::Stmt;

namespace {

struct Planned {
    std::vector<ir::TestPlan> plans;
    core::Diagnostics         diagnostics;
};

Planned plan(const std::string& header, core::Config config = {}, const std::string& include = "foo.h",
             ir::Framework framework = ir::Framework::GTest) {
    Planned p;
    const ir::Model model = frontend::read_header({"foo.h", header}, include, {}, p.diagnostics);
    backend::PlannerOptions options;
    options.config = std::move(config);
    options.framework = framework;
    options.header_include = include;
    p.plans = backend::plan_tests(model, options, p.diagnostics);
    return p;
}

Planned plan_qt(const std::string& header, core::Config config = {}) {
    return plan(header, std::move(config), "foo.h", ir::Framework::QTest);
}

const ir::TestSuite& suite(const Planned& p, std::size_t plan_index = 0) {
    static const ir::TestSuite none;
    EXPECT_LT(plan_index, p.plans.size());
    if (plan_index >= p.plans.size() || p.plans[plan_index].suites.empty()) return none;
    return p.plans[plan_index].suites[0];
}

std::vector<std::string> case_names(const Planned& p, std::size_t plan_index = 0) {
    std::vector<std::string> out;
    for (const ir::TestCase& c : suite(p, plan_index).cases) out.push_back(c.name);
    return out;
}

const ir::TestCase& the_case(const Planned& p, const char* name, std::size_t plan_index = 0) {
    static const ir::TestCase none;
    for (const ir::TestCase& c : suite(p, plan_index).cases) {
        if (c.name == name) return c;
    }
    ADD_FAILURE() << "no case " << name;
    return none;
}

// A statement as the backends will see it, in one line:
//   "placeholder: ...", "blank", "// comment", "code;", "// code;" (commented out),
//   "static_assert(cond, message)".
std::string show(const Stmt& s) {
    switch (s.kind) {
        case Stmt::Kind::Blank: return "blank";
        case Stmt::Kind::Comment: return "// " + s.text;
        case Stmt::Kind::Code: return (s.commented_out ? "// " : "") + s.text + ";";
        case Stmt::Kind::StaticAssert: return std::string(s.commented_out ? "// " : "") + "static_assert(" + s.text + ", \"" + s.message + "\")";
        case Stmt::Kind::Placeholder: return "placeholder: " + s.text;
        case Stmt::Kind::Data: return "data";
    }
    return {};
}

std::vector<std::string> body(const Planned& p, const char* name, std::size_t plan_index = 0) {
    std::vector<std::string> out;
    for (const Stmt& s : the_case(p, name, plan_index).body) out.push_back(show(s));
    return out;
}

using Lines = std::vector<std::string>;

std::vector<std::string> codes(const Planned& p) {
    std::vector<std::string> out;
    for (const core::Diagnostic& d : p.diagnostics.sorted()) out.push_back(d.code);
    return out;
}

// ---- the shape of a plan ------------------------------------------------------------------------------------------

TEST(TestPlannerPlan, OnePlanPerClassNamedFromItAndTheSuiteInItsNamespace) {
    const Planned p = plan("namespace acme { class FooBar { public: void run(); }; class Baz { public: void go(); }; }");
    ASSERT_EQ(p.plans.size(), 2u);
    EXPECT_EQ(p.plans[0].file, "foo_bar_test.cpp");
    EXPECT_EQ(p.plans[1].file, "baz_test.cpp");
    EXPECT_EQ(suite(p, 0).name, "FooBarTest");
    EXPECT_EQ(suite(p, 0).target_class, "FooBar");
    EXPECT_EQ(suite(p, 0).ns, std::vector<std::string>{"acme"});
    EXPECT_FALSE(suite(p, 0).fixture.has_value());
}

TEST(TestPlannerPlan, TheNamingPatternsDecideTheFileAndTheSuite) {
    core::Config config;
    config.naming.gtest_file = "test_{stem}.cc";
    config.naming.suite = "{Class}Spec";
    const Planned p = plan("class HTTPServer { public: void run(); };", config);
    ASSERT_EQ(p.plans.size(), 1u);
    EXPECT_EQ(p.plans[0].file, "test_http_server.cc");
    EXPECT_EQ(suite(p).name, "HTTPServerSpec");
}

TEST(TestPlannerPlan, IncludesAreTheFrameworkTheHeaderAndUtilityOnlyIfNeeded) {
    const Planned p = plan("class Foo { public: void run(); };", {}, "acme/foo.h");
    ASSERT_EQ(p.plans.size(), 1u);
    const auto& inc = p.plans[0].includes;
    ASSERT_EQ(inc.size(), 2u);
    EXPECT_EQ(inc[0].path, "gtest/gtest.h");
    EXPECT_TRUE(inc[0].is_system);
    EXPECT_EQ(inc[1].path, "acme/foo.h");
    EXPECT_FALSE(inc[1].is_system);

    const Planned moving = plan("class Foo { public: void take(std::string&& s); };", {}, "foo.h");
    ASSERT_EQ(moving.plans[0].includes.size(), 3u);
    EXPECT_EQ(moving.plans[0].includes[2].path, "utility");
    EXPECT_TRUE(moving.plans[0].includes[2].is_system);
}

TEST(TestPlannerPlan, CasesFollowTheDeclarationsAndOnlyThePublicOnesCount) {
    const Planned p = plan(
        "class Foo {\npublic:\n  Foo();\n  ~Foo();\n  int size() const;\n  Foo(const Foo&) = delete;\n  void gone() = delete;\n"
        "protected:\n  void guarded();\nprivate:\n  void hidden();\n"
        "public:\n  template <typename T> void tpl(T);\n  void last();\n};\n");
    EXPECT_EQ(case_names(p), (Lines{"Constructs", "Destructs", "size", "last"}));
}

TEST(TestPlannerPlan, SignalsAreNotTested) {
    const Planned p = plan("class W : public QObject { Q_OBJECT\npublic:\n  void run();\nsignals:\n  void changed();\npublic slots:\n  void apply();\n};\n");
    EXPECT_EQ(case_names(p), (Lines{"run", "apply"}));
}

TEST(TestPlannerPlan, AStructIsAsTestableAsAClass) {
    const Planned p = plan("struct S { void f(); };");
    EXPECT_EQ(case_names(p), Lines{"f"});
}

TEST(TestPlannerPlan, ClassTemplatesAreSkippedWithAWarning) {
    const Planned p = plan("template <typename T> class Box { public: void put(T); };\nclass Ok { public: void run(); };");
    ASSERT_EQ(p.plans.size(), 1u);
    EXPECT_EQ(suite(p).target_class, "Ok");
    ASSERT_EQ(codes(p), Lines{"mt-unsupported"});
    EXPECT_EQ(p.diagnostics.sorted()[0].severity, core::Severity::Warning);
    EXPECT_NE(p.diagnostics.sorted()[0].message.find("'Box'"), std::string::npos);
}

TEST(TestPlannerPlan, AClassWithNothingPublicIsANote) {
    const Planned p = plan("class Hidden { void a(); private: void b(); };");
    EXPECT_TRUE(p.plans.empty());
    ASSERT_EQ(codes(p), Lines{"mt-unsupported"});
    EXPECT_EQ(p.diagnostics.sorted()[0].severity, core::Severity::Note);
}

TEST(TestPlannerPlan, NothingToPlanIsNothing) {
    EXPECT_TRUE(plan("").plans.empty());
    EXPECT_TRUE(plan("int free_function(int);").plans.empty());
}

// ---- a method ------------------------------------------------------------------------------------------------------------

TEST(TestPlannerBody, TheConstMethodOfTheDesignDocument) {
    const Planned p = plan("namespace acme { class Foo { public: unsigned int size() const; }; }");
    EXPECT_EQ(body(p, "size"), (Lines{
        "placeholder: TODO(mt): write the test for acme::Foo::size() const",
        "blank",
        "// Arrange",
        "Foo sut;",
        "// Act",
        "const Foo& const_sut = sut;",
        "const unsigned int result = const_sut.size();",
        "(void)result;",
        "// Assert",
        "// TODO(mt): compare result with the expected value",
    }));
    EXPECT_EQ(the_case(p, "size").target, "acme::Foo::size() const");
}

TEST(TestPlannerBody, AVoidMethodIsJustTheCall) {
    const Planned p = plan("class Foo { public: void run(); };");
    EXPECT_EQ(body(p, "run"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::run()", "blank", "// Arrange", "Foo sut;", "// Act", "sut.run();",
        "// Assert", "// TODO(mt): check the effect of the call"}));
}

TEST(TestPlannerBody, ArgumentsAreLocalsOfTheTypeWithoutConstAndReference) {
    const Planned p = plan("class Foo { public: int f(const std::string& name, int count, const char* text, std::vector<int>& ids, Foo* other); };");
    EXPECT_EQ(body(p, "f"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::f(const std::string&, int, const char*, std::vector<int>&, Foo*)",
        "blank", "// Arrange", "Foo sut;",
        "std::string name{};", "int count{};", "const char* text{};", "std::vector<int> ids{};", "Foo* other{};",
        "// Act", "const int result = sut.f(name, count, text, ids, other);", "(void)result;",
        "// Assert", "// TODO(mt): compare result with the expected value"}));
}

TEST(TestPlannerBody, UnnamedParametersAreArgNAndNamesDoNotCollide) {
    const Planned p = plan("class Foo { public: void f(int, int sut, int result, int x, int x); };");
    const std::vector<std::string> lines = body(p, "f");
    EXPECT_EQ(lines[3], "Foo sut;");
    EXPECT_EQ(lines[4], "int arg1{};");
    EXPECT_EQ(lines[5], "int sut2{};");
    EXPECT_EQ(lines[6], "int result2{};");
    EXPECT_EQ(lines[7], "int x{};");
    EXPECT_EQ(lines[8], "int x2{};");
    EXPECT_EQ(lines[10], "sut.f(arg1, sut2, result2, x, x2);");
}

TEST(TestPlannerBody, ParametersWithDefaultsAreLeftOutAndSaid) {
    const Planned p = plan("class Foo { public: void f(int a, int b = 2, int c = 3); };");
    EXPECT_EQ(body(p, "f"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::f(int, int, int)", "blank", "// Arrange", "Foo sut;", "int a{};",
        "// TODO(mt): the arguments that have default values are left out", "// Act", "sut.f(a);",
        "// Assert", "// TODO(mt): check the effect of the call"}));
}

TEST(TestPlannerBody, RvalueReferencesAreMovedIn) {
    const Planned p = plan("class Foo { public: void take(std::string&& s, Foo&& other); };");
    const std::vector<std::string> lines = body(p, "take");
    EXPECT_EQ(lines[4], "std::string s{};");
    EXPECT_EQ(lines[5], "Foo other{};") << "Foo is default constructible";
    EXPECT_EQ(lines[7], "sut.take(std::move(s), std::move(other));");
    ASSERT_EQ(p.plans.size(), 1u);
    ASSERT_EQ(p.plans[0].includes.size(), 3u);
    EXPECT_EQ(p.plans[0].includes[2].path, "utility");
    EXPECT_TRUE(p.plans[0].includes[2].is_system);
}

TEST(TestPlannerBody, StaticMethodsNeedNoObject) {
    const Planned p = plan("class Foo { public: static Foo make(int seed); };");
    EXPECT_EQ(body(p, "make"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::make(int)", "blank", "// Arrange", "int seed{};", "// Act",
        "const Foo result = Foo::make(seed);", "(void)result;", "// Assert", "// TODO(mt): compare result with the expected value"}));
}

TEST(TestPlannerBody, ANoexceptMethodGetsAStaticAssertion) {
    const Planned p = plan("class Foo { public: int get(int i) const noexcept; };");
    const std::vector<std::string> lines = body(p, "get");
    ASSERT_GT(lines.size(), 10u);
    EXPECT_EQ(lines[9], "// Assert");
    EXPECT_EQ(lines[10], "static_assert(noexcept(const_sut.get(i)), \"Foo::get(int) const is declared noexcept\")");
}

TEST(TestPlannerBody, ReturnTypesDeclareTheResultTheRightWay) {
    const Planned p = plan(
        "class Foo { public:\n"
        "  int value();\n"
        "  const char* text();\n"
        "  char* const fixed();\n"
        "  Foo* self();\n"
        "  const std::string& name();\n"
        "  std::string& mut();\n"
        "  std::string&& moved();\n"
        "  void (*callback())(int);\n"
        "  auto deduced() { return 1; }\n"
        "  std::vector<int> items();\n"
        "};");
    const auto result = [&](const char* name) {
        for (const std::string& l : body(p, name)) {
            if (l.find(" result = ") != std::string::npos) return l.substr(0, l.find(" = "));
        }
        return std::string("<none>");
    };
    EXPECT_EQ(result("value"), "const int result");
    EXPECT_EQ(result("text"), "const char* const result");
    EXPECT_EQ(result("fixed"), "char* const result");
    EXPECT_EQ(result("self"), "Foo* const result");
    EXPECT_EQ(result("name"), "const std::string& result");
    EXPECT_EQ(result("mut"), "std::string& result");
    EXPECT_EQ(result("moved"), "std::string&& result");
    EXPECT_EQ(result("deduced"), "auto result");
    EXPECT_EQ(result("items"), "const std::vector<int> result");
}

// ---- constructors and destructors -----------------------------------------------------------------------------------------

TEST(TestPlannerBody, ConstructorsAreCasesOfTheirOwn) {
    const Planned p = plan("class Foo { public: Foo(); explicit Foo(int n); Foo(int a, const std::string& b); };");
    EXPECT_EQ(case_names(p), (Lines{"Constructs", "Constructs_int", "Constructs_int_std_string"}));
    EXPECT_EQ(body(p, "Constructs_int"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::Foo(int)", "blank", "// Arrange", "int n{};", "// Act", "Foo sut(n);",
        "(void)sut;", "// Assert", "// TODO(mt): check the state of sut"}));
    EXPECT_EQ(body(p, "Constructs"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::Foo()", "blank", "// Arrange", "// Act", "Foo sut;", "(void)sut;",
        "// Assert", "// TODO(mt): check the state of sut"}));
}

TEST(TestPlannerBody, ADestructorIsACase) {
    const Planned p = plan("class Foo { public: ~Foo(); };");
    EXPECT_EQ(case_names(p), Lines{"Destructs"});
    EXPECT_EQ(body(p, "Destructs"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::~Foo()", "blank", "// Arrange", "Foo sut;", "// Act",
        "// sut is destroyed when the test ends", "(void)sut;", "// Assert", "// TODO(mt): check what the destructor releases"}));
}

TEST(TestPlannerBody, ACopyConstructorBuildsItsSourceWhenTheClassCanBeDefaultConstructed) {
    const Planned p = plan("class Foo { public: Foo(); Foo(const Foo& other); };");
    EXPECT_EQ(body(p, "Constructs_foo")[3], "Foo other{};");
    EXPECT_EQ(body(p, "Constructs_foo")[5], "Foo sut(other);");
}

// ---- the object under test ---------------------------------------------------------------------------------------------

TEST(TestPlannerSut, WithoutADefaultConstructorTheSimplestOneThatCanBeCalledIsUsed) {
    const Planned p = plan("class Foo { public: Foo(int a, int b); Foo(const char* name); explicit Foo(Mystery m); void run(); };");
    const std::vector<std::string> lines = body(p, "run");
    EXPECT_EQ(lines, (Lines{"placeholder: TODO(mt): write the test for Foo::run()", "blank", "// Arrange", "const char* name{};",
                            "Foo sut(name);", "// Act", "sut.run();", "// Assert", "// TODO(mt): check the effect of the call"}));
}

TEST(TestPlannerSut, TheFirstOfEquallySimpleConstructorsWins) {
    const Planned p = plan("class Foo { public: Foo(int a); Foo(double b); void run(); };");
    EXPECT_EQ(body(p, "run")[3], "int a{};");
    EXPECT_EQ(body(p, "run")[4], "Foo sut(a);");
}

TEST(TestPlannerSut, ADefaultConstructorBeatsEverything) {
    const Planned p = plan("class Foo { public: Foo(int a); Foo(); void run(); };");
    EXPECT_EQ(body(p, "run")[3], "Foo sut;");
}

TEST(TestPlannerSut, AllDefaultedArgumentsCountAsADefaultConstructor) {
    const Planned p = plan("class Foo { public: explicit Foo(int a = 1); void run(); };");
    EXPECT_EQ(body(p, "run")[3], "Foo sut;");
}

TEST(TestPlannerSut, WhenNoConstructorCanBeCalledTheCallIsKeptAsAComment) {
    const Planned p = plan("class Foo { public: explicit Foo(Mystery m); int get() const; };");
    EXPECT_EQ(body(p, "get"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::get() const", "blank", "// Arrange",
        "// TODO(mt): 'Foo' has no public constructor that can be called with default values: build sut by hand",
        "// Foo sut;", "// Act", "// const Foo& const_sut = sut;", "// const int result = const_sut.get();", "// (void)result;",
        "// Assert", "// TODO(mt): compare result with the expected value"}));
}

TEST(TestPlannerSut, AnAbstractClassCannotBeBuilt) {
    const Planned p = plan("class Shape { public: virtual double area() const = 0; virtual ~Shape(); };");
    EXPECT_EQ(case_names(p), (Lines{"area", "Destructs"}));
    EXPECT_EQ(body(p, "area")[3], "// TODO(mt): 'Shape' is abstract: derive a test double and build that");
    EXPECT_EQ(body(p, "area")[4], "// Shape sut;");
}

TEST(TestPlannerSut, ConstructorTestsOfAnAbstractClassAreCommentedOut) {
    const Planned p = plan("class Shape { public: Shape(); virtual double area() const = 0; };");
    const std::vector<std::string> lines = body(p, "Constructs");
    EXPECT_EQ(lines[3], "// TODO(mt): 'Shape' is abstract: construct a class derived from it instead");
    EXPECT_EQ(lines[4], "// Act");
    EXPECT_EQ(lines[5], "// Shape sut;");
}

TEST(TestPlannerSut, APrivateDefaultConstructorDoesNotCount) {
    const Planned p = plan("class Foo { Foo(); public: explicit Foo(int a); void run(); };");
    EXPECT_EQ(body(p, "run")[3], "int a{};");
}

TEST(TestPlannerSut, TheConfigurationCanVouchForAType) {
    core::Config config;
    config.default_constructible_types = {"Handle"};
    EXPECT_EQ(body(plan("class Foo { public: void f(Handle h); };", config), "f")[4], "Handle h{};");
    EXPECT_EQ(body(plan("class Foo { public: void f(Handle h); };"), "f")[4], "// TODO(mt): an argument of type 'Handle' cannot be built here");
}

// ---- arguments that cannot be built -----------------------------------------------------------------------------------

TEST(TestPlannerArguments, AnUnbuildableArgumentCommentsOutTheCall) {
    const Planned p = plan("class Foo { public: int f(int ok, Mystery bad, int never_reached); };");
    // Everything the call needs goes into comments with it: a local nobody uses is a warning in the generated file.
    EXPECT_EQ(body(p, "f"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::f(int, Mystery, int)", "blank", "// Arrange", "// Foo sut;",
        "// TODO(mt): an argument of type 'Mystery' cannot be built here", "// int ok{};", "// Mystery bad{};",
        "// int never_reached{};", "// Act", "// const int result = sut.f(ok, bad, never_reached);", "// (void)result;",
        "// Assert", "// TODO(mt): compare result with the expected value"}));
}

TEST(TestPlannerArguments, TheMessageNamesTheTypeToBuildNotTheReference) {
    const Planned p = plan("class Foo { public: void f(const Mystery& m, Other&& o); };");
    EXPECT_EQ(body(p, "f")[4], "// TODO(mt): an argument of type 'Mystery' cannot be built here");
}

TEST(TestPlannerArguments, AMoveInACommentedOutCallNeedsNoUtilityHeader) {
    const Planned p = plan("class Foo { public: void f(Mystery m, std::string&& s); };");
    ASSERT_EQ(p.plans.size(), 1u);
    EXPECT_EQ(body(p, "f")[8], "// sut.f(m, std::move(s));");
    EXPECT_EQ(p.plans[0].includes.size(), 2u) << "gtest and the header, no <utility>";
}

TEST(TestPlannerArguments, FunctionPointersAndArraysCannotBeBuilt) {
    const Planned p = plan("class Foo { public: void f(void (*cb)(int)); };");
    EXPECT_EQ(body(p, "f")[4], "// TODO(mt): an argument of type 'void(*)(int)' cannot be built here");
}

TEST(TestPlannerArguments, TheNoexceptAssertionIsCommentedOutWithTheCall) {
    const Planned p = plan("class Foo { public: void f(Mystery m) noexcept; };");
    bool commented = false;
    for (const std::string& l : body(p, "f")) commented = commented || l.rfind("// static_assert(", 0) == 0;
    EXPECT_TRUE(commented);
}

// ---- nested types -----------------------------------------------------------------------------------------------------------

TEST(TestPlannerNested, NestedTypesAreQualifiedWithTheirClass) {
    const Planned p = plan(
        "class Foo { public:\n  struct Item { int x; };\n  enum class Mode { A };\n"
        "  Item make(Mode m, const Item& seed) const;\n};");
    EXPECT_EQ(body(p, "make"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::make(Mode, const Item&) const", "blank", "// Arrange", "Foo sut;",
        "Foo::Mode m{};", "Foo::Item seed{};", "// Act", "const Foo& const_sut = sut;",
        "const Foo::Item result = const_sut.make(m, seed);", "(void)result;", "// Assert",
        "// TODO(mt): compare result with the expected value"}));
}

TEST(TestPlannerNested, ANestedClassThatNeedsArgumentsIsNotBuildable) {
    const Planned p = plan("class Foo { public:\n  struct Item { explicit Item(int v); };\n  void put(const Item& i);\n};");
    EXPECT_EQ(body(p, "put")[4], "// TODO(mt): an argument of type 'Foo::Item' cannot be built here");
}

TEST(TestPlannerNested, ANonPublicEnumCannotBeBuiltFromTheTest) {
    const Planned p = plan("class Foo { enum class Mode { A }; public:\n  void set(Mode m);\n};");
    EXPECT_EQ(body(p, "set")[4], "// TODO(mt): an argument of type 'Foo::Mode' cannot be built here");
}

TEST(TestPlannerNested, AQualifiedMentionIsNotQualifiedTwice) {
    const Planned p = plan("class Foo { public:\n  struct Item { int x; };\n  void put(Foo::Item i, ns::Item other);\n};");
    const std::vector<std::string> lines = body(p, "put");
    EXPECT_EQ(lines[4], "// TODO(mt): an argument of type 'ns::Item' cannot be built here");
    EXPECT_EQ(lines[5], "// Foo::Item i{};") << "Foo::Item is fine, but the call it belongs to is commented out";
    EXPECT_EQ(lines[6], "// ns::Item other{};") << "and the qualified name is left as written";
}

// ---- operators ---------------------------------------------------------------------------------------------------------------

TEST(TestPlannerOperators, OperatorsAreCalledByTheirNameAndNamedFromATable) {
    const Planned p = plan("class V { public: bool operator==(const V& o) const; int& operator[](int i); explicit operator bool() const; };");
    EXPECT_EQ(case_names(p), (Lines{"OperatorEq", "OperatorIndex", "ConversionToBool"}));
    EXPECT_EQ(body(p, "OperatorEq")[7], "const bool result = const_sut.operator==(o);");
    EXPECT_EQ(body(p, "OperatorIndex")[6], "int& result = sut.operator[](i);");
    EXPECT_EQ(body(p, "ConversionToBool")[6], "const bool result = const_sut.operator bool();");
}

// ---- names ------------------------------------------------------------------------------------------------------------------------

TEST(TestPlannerNames, OverloadsAreToldApartByTheirTypesAndTheRestByANumber) {
    const Planned p = plan("class Foo { public: void set(int v); void set(const std::string& s); int size(); int size() const; };");
    EXPECT_EQ(case_names(p), (Lines{"set_int", "set_std_string", "size", "size_2"}));
}

TEST(TestPlannerNames, EveryNameIsAValidAndUniqueIdentifier) {
    const Planned p = plan(
        "class Foo { public: Foo(); Foo(int); Foo(double); ~Foo(); bool operator==(const Foo&) const; bool operator!=(const Foo&) const;\n"
        "Foo& operator=(const Foo&); Foo& operator=(Foo&&); int operator()(int); operator int() const; operator double() const; };");
    std::set<std::string> seen;
    for (const std::string& n : case_names(p)) {
        EXPECT_TRUE(core::is_identifier(n)) << n;
        EXPECT_TRUE(seen.insert(n).second) << "duplicate " << n;
    }
    EXPECT_EQ(seen.size(), 11u);
}

// ---- robustness --------------------------------------------------------------------------------------------------------------------

TEST(TestPlannerWhole, TheSameHeaderGivesTheSamePlan) {
    const std::string header = "namespace a { class Foo { public: Foo(int); int f(const std::string&) const; static Foo g(); }; }";
    const Planned a = plan(header);
    const Planned b = plan(header);
    ASSERT_EQ(a.plans.size(), b.plans.size());
    for (std::size_t i = 0; i < a.plans.size(); ++i) {
        for (std::size_t c = 0; c < suite(a, i).cases.size(); ++c) {
            EXPECT_EQ(body(a, suite(a, i).cases[c].name.c_str(), i), body(b, suite(b, i).cases[c].name.c_str(), i));
        }
    }
}

TEST(TestPlannerWhole, EveryPrefixOfAHeaderIsPlannedWithoutIncident) {
    const std::string header =
        "namespace acme {\nclass Foo : public Base {\npublic:\n  struct Item { int x; };\n  explicit Foo(int a, const Item& i = {});\n"
        "  virtual ~Foo();\n  static Foo make(); unsigned size() const noexcept;\n  void take(std::string&& s);\n  bool operator==(const Foo&) const;\n"
        "protected:\n  void hidden();\n};\ntemplate <typename T> class Box { public: void put(T); };\n}\n";
    for (std::size_t n = 0; n <= header.size(); ++n) {
        const Planned p = plan(header.substr(0, n));
        for (const ir::TestPlan& tp : p.plans) {
            for (const ir::TestSuite& s : tp.suites) {
                for (const ir::TestCase& c : s.cases) {
                    ASSERT_FALSE(c.body.empty()) << "cut at " << n;
                    EXPECT_EQ(c.body.front().kind, Stmt::Kind::Placeholder) << "cut at " << n;
                }
            }
        }
    }
}

// ---- what mutation testing asked for -------------------------------------------------------------------------------

TEST(TestPlannerSut, ADeletedConstructorIsNotUsedToBuildTheObject) {
    const Planned p = plan("class Foo { public: Foo(int a) = delete; Foo(int a, int b); void run(); };");
    const std::vector<std::string> lines = body(p, "run");
    EXPECT_EQ(lines[3], "int a{};");
    EXPECT_EQ(lines[4], "int b{};");
    EXPECT_EQ(lines[5], "Foo sut(a, b);");
}

TEST(TestPlannerSut, AConstructorThatTakesAnRvalueNeedsTheUtilityHeader) {
    const Planned p = plan("class Foo { public: explicit Foo(std::string&& s); void run(); };");
    EXPECT_EQ(body(p, "run")[4], "Foo sut(std::move(s));");
    ASSERT_EQ(p.plans.size(), 1u);
    ASSERT_EQ(p.plans[0].includes.size(), 3u);
    EXPECT_EQ(p.plans[0].includes[2].path, "utility");
}

TEST(TestPlannerSut, TheObjectAndItsArgumentsAreCommentedOutWithACallThatCannotBeBuilt) {
    const Planned p = plan("class Foo { public: explicit Foo(std::string&& s); void run(Mystery m); };");
    const std::vector<std::string> lines = body(p, "run");
    EXPECT_EQ(lines[3], "// std::string s{};");
    EXPECT_EQ(lines[4], "// Foo sut(std::move(s));");
    EXPECT_EQ(lines[5], "// TODO(mt): an argument of type 'Mystery' cannot be built here");
    ASSERT_EQ(p.plans.size(), 1u);
    EXPECT_EQ(p.plans[0].includes.size(), 3u) << "the constructor's own case moves, and that asks for <utility>";
}

TEST(TestPlannerBody, ANameOfTheSkeletonIsNotReusedByAParameter) {
    const Planned p = plan("class Foo { public: void f(int const_sut) const; };");
    const std::vector<std::string> lines = body(p, "f");
    EXPECT_EQ(lines[4], "int const_sut2{};");
    EXPECT_EQ(lines[6], "const Foo& const_sut = sut;");
    EXPECT_EQ(lines[7], "const_sut.f(const_sut2);");
}

TEST(TestPlannerBody, ConstructorsWithUnbuildableArgumentsAreCommentedOut) {
    const Planned p = plan("class Foo { public: Foo(int a, Mystery m, std::string&& s); };");
    EXPECT_EQ(body(p, "Constructs"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::Foo(int, Mystery, std::string&&)", "blank", "// Arrange",
        "// TODO(mt): an argument of type 'Mystery' cannot be built here", "// int a{};", "// Mystery m{};",
        "// std::string s{};", "// Act", "// Foo sut(a, m, std::move(s));", "// (void)sut;",
        "// Assert", "// TODO(mt): check the state of sut"}));
    ASSERT_EQ(p.plans.size(), 1u);
    EXPECT_EQ(p.plans[0].includes.size(), 2u);
}

TEST(TestPlannerBody, ConstructorsLeaveOutTheirDefaultsAndSayIt) {
    const Planned p = plan("class Foo { public: Foo(int a, int b = 3); };");
    EXPECT_EQ(body(p, "Constructs"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::Foo(int, int)", "blank", "// Arrange", "int a{};",
        "// TODO(mt): the arguments that have default values are left out", "// Act", "Foo sut(a);", "(void)sut;", "// Assert",
        "// TODO(mt): check the state of sut"}));
}

TEST(TestPlannerBody, VariadicArgumentsAreLeftOutAndSaid) {
    const Planned p = plan("class Foo { public: void log(const char* fmt, ...); };");
    EXPECT_EQ(body(p, "log"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::log(const char*, ...)", "blank", "// Arrange", "Foo sut;",
        "const char* fmt{};", "// TODO(mt): the variadic arguments are left out", "// Act", "sut.log(fmt);", "// Assert",
        "// TODO(mt): check the effect of the call"}));
    const Planned only = plan("class Foo { public: void v(...); };");
    EXPECT_EQ(body(only, "v")[6], "sut.v();");
}

TEST(TestPlannerBody, ADefaultOutranksTheVariadicEllipsis) {
    const Planned p = plan("class Foo { public: void f(int a = 1, ...); };");
    EXPECT_EQ(body(p, "f")[4], "// TODO(mt): the arguments that have default values are left out");
}

TEST(TestPlannerBody, AConstValueResultIsNotConstTwice) {
    const Planned p = plan("class Foo { public: const std::string name(); const Foo make(); };");
    EXPECT_EQ(body(p, "name")[5], "const std::string result = sut.name();");
    EXPECT_EQ(body(p, "make")[5], "const Foo result = sut.make();");
}

TEST(TestPlannerBody, ReferencesToArraysAreDeclaredWithAuto) {
    const Planned p = plan("class Foo { public: int (&table())[3]; };");
    EXPECT_EQ(body(p, "table")[5], "auto result = sut.table();");
}

TEST(TestPlannerBody, TheTopLevelConstOfAParameterIsNotKept) {
    const Planned p = plan("class Foo { public: void f(char* const p, const int n, const char* q, int* const* r); };");
    const std::vector<std::string> lines = body(p, "f");
    EXPECT_EQ(lines[4], "char* p{};");
    EXPECT_EQ(lines[5], "int n{};");
    EXPECT_EQ(lines[6], "const char* q{};");
    EXPECT_EQ(lines[7], "int* const* r{};") << "this const belongs to the pointee";
}

TEST(TestPlannerNested, ANumberIsNeverTakenForANestedType) {
    const Planned p = plan("class Foo { public: struct u {}; void f(std::array<int, 3u> a, u b); };");
    const std::vector<std::string> lines = body(p, "f");
    EXPECT_EQ(lines[4], "std::array<int, 3u> a{};") << "3u keeps its u";
    EXPECT_EQ(lines[5], "Foo::u b{};");
}

TEST(TestPlannerNested, TheDigitSeparatorsAndSuffixesOfANumberAreNotNames) {
    const Planned p = plan("class Foo { public: struct B {}; struct f {}; void g(std::array<int, 0xA'B> a, std::array<int, 1.f> b); };");
    const std::vector<std::string> lines = body(p, "g");
    EXPECT_EQ(lines[4], "std::array<int, 0xA'B> a{};");
    EXPECT_EQ(lines[5], "std::array<int, 1.f> b{};");
}

TEST(TestPlannerSut, TheDestructorOfAClassThatCannotBeBuiltIsCommentedOut) {
    const Planned p = plan("class Foo { public: explicit Foo(Mystery m); ~Foo(); };");
    EXPECT_EQ(body(p, "Destructs"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::~Foo()", "blank", "// Arrange",
        "// TODO(mt): 'Foo' has no public constructor that can be called with default values: build sut by hand",
        "// Foo sut;", "// Act", "// sut is destroyed when the test ends", "// (void)sut;", "// Assert",
        "// TODO(mt): check what the destructor releases"}));
}

TEST(TestPlannerPlan, TheNeedForUtilityDoesNotLeakFromOneClassToTheNext) {
    const Planned p = plan("class A { public: void take(std::string&& s); };\nclass B { public: void go(); };");
    ASSERT_EQ(p.plans.size(), 2u);
    EXPECT_EQ(p.plans[0].includes.size(), 3u);
    EXPECT_EQ(p.plans[1].includes.size(), 2u);
}

TEST(TestPlannerPlan, TheNeedForUtilityDoesNotLeakFromOneCaseToTheNext) {
    const Planned p = plan("class A { public: void go(); void take(std::string&& s); void again(); };");
    ASSERT_EQ(p.plans.size(), 1u);
    EXPECT_EQ(p.plans[0].includes.size(), 3u) << "once is enough, and one more at the end is not another one";
}

// ---- inputs only a hand-made model can give ---------------------------------------------------------------------------

Planned plan_model(ir::Model model) {
    Planned p;
    backend::PlannerOptions options;
    options.header_include = "foo.h";
    p.plans = backend::plan_tests(model, options, p.diagnostics);
    return p;
}

ir::Model one_method(ir::Method m) {
    ir::Class c;
    c.name = "Foo";
    m.access = ir::Access::Public;
    c.methods.push_back(std::move(m));
    ir::Model model;
    model.classes.push_back(std::move(c));
    return model;
}

TEST(TestPlannerHandMade, AMethodWithoutAReturnTypeIsJustACall) {
    ir::Method m;
    m.name = "mystery";
    const Planned p = plan_model(one_method(m));
    EXPECT_EQ(body(p, "mystery")[5], "sut.mystery();");
}

TEST(TestPlannerHandMade, AConversionOperatorWithoutATypeDoesNotThrow) {
    ir::Method m;
    m.name = "operator";
    m.kind = ir::MethodKind::Conversion;
    const Planned p = plan_model(one_method(m));
    ASSERT_EQ(p.plans.size(), 1u);
    EXPECT_EQ(body(p, p.plans[0].suites[0].cases[0].name.c_str())[5], "auto result = sut.operator();");
}

// ---- Qt Test ------------------------------------------------------------------------------------------------------------

TEST(TestPlannerQt, TheFileTheClassAndTheIncludeAreQtTests) {
    const Planned p = plan_qt("namespace acme { class FooBar { public: void run(); }; }");
    ASSERT_EQ(p.plans.size(), 1u);
    EXPECT_EQ(p.plans[0].file, "tst_foo_bar.cpp");
    EXPECT_EQ(suite(p).name, "TestFooBar");
    EXPECT_EQ(suite(p).target_class, "FooBar");
    EXPECT_EQ(suite(p).ns, std::vector<std::string>{"acme"});
    ASSERT_EQ(p.plans[0].includes.size(), 2u);
    EXPECT_EQ(p.plans[0].includes[0].path, "QtTest");
    EXPECT_TRUE(p.plans[0].includes[0].is_system);
    EXPECT_EQ(p.plans[0].includes[1].path, "foo.h");
}

TEST(TestPlannerQt, TheNamingPatternsDecideTheFileAndTheClass) {
    core::Config config;
    config.naming.qtest_file = "{stem}_qt.cpp";
    config.naming.qtest_class = "{Class}Spec";
    const Planned p = plan_qt("class HTTPServer { public: void run(); };", config);
    ASSERT_EQ(p.plans.size(), 1u);
    EXPECT_EQ(p.plans[0].file, "http_server_qt.cpp");
    EXPECT_EQ(suite(p).name, "HTTPServerSpec");
}

TEST(TestPlannerQt, TheGoogleTestPatternsAreNotUsed) {
    core::Config config;
    config.naming.gtest_file = "wrong_{stem}.cpp";
    config.naming.suite = "Wrong{Class}";
    const Planned p = plan_qt("class Foo { public: void run(); };", config);
    EXPECT_EQ(p.plans[0].file, "tst_foo.cpp");
    EXPECT_EQ(suite(p).name, "TestFoo");
}

TEST(TestPlannerQt, TheBodyOfAMethodIsTheSameAsForGoogleTest) {
    const std::string header = "class Foo { public: int get(int i) const noexcept; };";
    const Planned qt = plan_qt(header);
    const Planned gt = plan(header);
    EXPECT_EQ(body(qt, "get"), body(gt, "get"));
}

TEST(TestPlannerQt, ASignalIsListenedToWithAQSignalSpy) {
    const Planned p = plan_qt("class W : public QObject { Q_OBJECT\npublic:\n  void run();\nsignals:\n  void changed(int value);\n};\n");
    EXPECT_EQ(case_names(p), (Lines{"run", "changed"}));
    EXPECT_EQ(body(p, "changed"), (Lines{
        "placeholder: TODO(mt): write the test for W::changed(int)", "blank", "// Arrange", "W sut;",
        "QSignalSpy spy(&sut, &W::changed);", "(void)spy;", "// Act", "// TODO(mt): make sut emit the signal",
        "// Assert", "// TODO(mt): check spy.count() and the arguments of spy.takeFirst()"}));
}

TEST(TestPlannerQt, AnOverloadedSignalNeedsTheSignatureToTakeItsAddress) {
    const Planned p = plan_qt(
        "class W : public QObject { Q_OBJECT\npublic:\n  struct Item { int x; };\nsignals:\n"
        "  void changed(int value);\n  void changed(const Item& item, const QString& why);\n  void other();\n};\n");
    EXPECT_EQ(case_names(p), (Lines{"changed_int", "changed_item_q_string", "other"}));
    EXPECT_EQ(body(p, "changed_int")[4], "QSignalSpy spy(&sut, static_cast<void (W::*)(int)>(&W::changed));");
    EXPECT_EQ(body(p, "changed_item_q_string")[4],
              "QSignalSpy spy(&sut, static_cast<void (W::*)(const W::Item&, const QString&)>(&W::changed));");
    EXPECT_EQ(body(p, "other")[4], "QSignalSpy spy(&sut, &W::other);") << "a signal that is alone needs no cast";
}

TEST(TestPlannerQt, ASlotOfTheSameNameMakesTheSignalAmbiguousToo) {
    const Planned p = plan_qt("class W : public QObject { Q_OBJECT\npublic slots:\n  void changed(int value);\nsignals:\n  void changed(const QString& text);\n};\n");
    EXPECT_EQ(case_names(p), (Lines{"changed_int", "changed_q_string"}));
    EXPECT_EQ(body(p, "changed_q_string")[4], "QSignalSpy spy(&sut, static_cast<void (W::*)(const QString&)>(&W::changed));");
}

TEST(TestPlannerQt, ASignalOfAnotherNameThanTheOthersNeedsNoCast) {
    const Planned p = plan_qt("class W : public QObject { Q_OBJECT\npublic slots:\n  void apply(int value);\nsignals:\n  void changed(int value);\n};\n");
    EXPECT_EQ(body(p, "changed")[4], "QSignalSpy spy(&sut, &W::changed);");
}

TEST(TestPlannerQt, ASignalOfAClassThatCannotBeBuiltIsAComment) {
    const Planned p = plan_qt("class W : public QObject { Q_OBJECT\npublic:\n  explicit W(Mystery m);\nsignals:\n  void changed();\n};\n");
    EXPECT_EQ(body(p, "changed"), (Lines{
        "placeholder: TODO(mt): write the test for W::changed()", "blank", "// Arrange",
        "// TODO(mt): 'W' has no public constructor that can be called with default values: build sut by hand", "// W sut;",
        "// QSignalSpy spy(&sut, &W::changed);", "// (void)spy;", "// Act", "// TODO(mt): make sut emit the signal",
        "// Assert", "// TODO(mt): check spy.count() and the arguments of spy.takeFirst()"}));
}

TEST(TestPlannerQt, TheSpyDoesNotTakeTheNameOfAnArgument) {
    const Planned p = plan_qt("class W : public QObject { Q_OBJECT\npublic:\n  explicit W(int spy);\nsignals:\n  void changed();\n};\n");
    const std::vector<std::string> lines = body(p, "changed");
    EXPECT_EQ(lines[3], "int spy2{};");
    EXPECT_EQ(lines[4], "W sut(spy2);");
}

TEST(TestPlannerQt, GoogleTestStillHasNothingToSayAboutASignal) {
    const Planned p = plan("class W : public QObject { Q_OBJECT\npublic:\n  void run();\nsignals:\n  void changed();\n};\n");
    EXPECT_EQ(case_names(p), Lines{"run"});
}

TEST(TestPlannerQt, ASlotIsAnOrdinaryMethod) {
    const Planned p = plan_qt("class W : public QObject { Q_OBJECT\npublic slots:\n  void apply(int n);\n};\n");
    EXPECT_EQ(body(p, "apply")[4], "int n{};");
    EXPECT_EQ(body(p, "apply")[6], "sut.apply(n);");
}

TEST(TestPlannerQt, ANameQtTestGivesAMeaningToIsNotUsedForASlot) {
    const Planned p = plan_qt("class Foo { public: void init(); void cleanup(); void initTestCase(); void cleanupTestCase(); "
                              "void size_data(); void size(); };");
    EXPECT_EQ(case_names(p), (Lines{"init_call", "cleanup_call", "initTestCase_call", "cleanupTestCase_call", "size_data_call", "size"}));
    EXPECT_EQ(the_case(p, "init_call").target, "Foo::init()") << "the message still names the method";
}

TEST(TestPlannerQt, TheRenamedSlotDoesNotTakeANameThatIsAlreadyThere) {
    const Planned p = plan_qt("class Foo { public: void init(); void init_call(); };");
    EXPECT_EQ(case_names(p), (Lines{"init_call_2", "init_call"}));
}

TEST(TestPlannerQt, AnOverloadSuffixThatEndsInDataIsRenamedToo) {
    const Planned p = plan_qt("class Foo { public: void x(Data d); void x(int i); };");
    EXPECT_EQ(case_names(p), (Lines{"x_data_call", "x_int"}));
}

TEST(TestPlannerQt, GoogleTestIsFreeToCallATestInit) {
    const Planned p = plan("class Foo { public: void init(); void size_data(); };");
    EXPECT_EQ(case_names(p), (Lines{"init", "size_data"}));
}

TEST(TestPlannerQt, AMovedArgumentStillNeedsUtility) {
    const Planned p = plan_qt("class Foo { public: void take(std::string&& s); };");
    ASSERT_EQ(p.plans[0].includes.size(), 3u);
    EXPECT_EQ(p.plans[0].includes[2].path, "utility");
}

// ---- a fixture (gtest.fixture) -------------------------------------------------------------------------------------------

Planned plan_fixture(const std::string& header, core::Config config = {}) {
    config.gtest.fixture = true;
    return plan(header, std::move(config));
}

const ir::Fixture& fixture_of_suite(const Planned& p, std::size_t plan_index = 0) {
    static const ir::Fixture none;
    const ir::TestSuite& s = suite(p, plan_index);
    EXPECT_TRUE(s.fixture.has_value());
    return s.fixture ? *s.fixture : none;
}

Lines fixture_members(const ir::Fixture& f) {
    Lines out;
    for (const Stmt& s : f.members) out.push_back(show(s));
    return out;
}

TEST(TestPlannerFixture, ThereIsNoneUnlessAskedFor) {
    EXPECT_FALSE(suite(plan("class Foo { public: void run(); };")).fixture.has_value());
    EXPECT_TRUE(suite(plan_fixture("class Foo { public: void run(); };")).fixture.has_value());
}

TEST(TestPlannerFixture, ItIsNamedLikeTheSuiteAndOwnsADefaultConstructedSut) {
    core::Config config;
    config.naming.suite = "{Class}Spec";
    const Planned p = plan_fixture("class Foo { public: void run(); };", config);
    const ir::Fixture& f = fixture_of_suite(p);
    EXPECT_EQ(f.name, "FooSpec");
    EXPECT_EQ(f.name, suite(p).name) << "TEST_F wants the suite and the fixture to be one name";
    EXPECT_EQ(fixture_members(f), Lines{"Foo sut;"});
    EXPECT_TRUE(f.initialisers.empty());
    ASSERT_EQ(f.set_up.size(), 1u);
    EXPECT_EQ(show(f.set_up[0]), "// TODO(mt): bring sut to the state every test starts from");
    EXPECT_TRUE(f.tear_down.empty());
}

TEST(TestPlannerFixture, ASutThatNeedsArgumentsGetsThemAsMembersAndAnInitialiser) {
    const Planned p = plan_fixture("class Account { public: Account(int id, const std::string& owner); explicit Account(int id); void deposit(long amount); };");
    const ir::Fixture& f = fixture_of_suite(p);
    EXPECT_EQ(fixture_members(f), (Lines{"int id{};", "Account sut;"}));
    EXPECT_EQ(f.initialisers, Lines{"sut(id)"});
}

TEST(TestPlannerFixture, TheBodyOfAMethodHasNoSutOfItsOwn) {
    const Planned p = plan_fixture("class Foo { public: unsigned int size() const; void take(int n); };");
    EXPECT_EQ(body(p, "size"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::size() const", "blank", "// Arrange", "// Act",
        "const Foo& const_sut = sut;", "const unsigned int result = const_sut.size();", "(void)result;", "// Assert",
        "// TODO(mt): compare result with the expected value"}));
    EXPECT_EQ(body(p, "take"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::take(int)", "blank", "// Arrange", "int n{};", "// Act", "sut.take(n);",
        "// Assert", "// TODO(mt): check the effect of the call"}));
}

TEST(TestPlannerFixture, ANameTheFixtureTakesIsNotTakenAgainByABody) {
    const Planned p = plan_fixture("class Account { public: explicit Account(int id); void deposit(int id, int object, int sut); };");
    EXPECT_EQ(fixture_members(fixture_of_suite(p)), (Lines{"int id{};", "Account sut;"}));
    EXPECT_EQ(body(p, "deposit")[3], "int id2{};") << "a local called id would shadow the member";
    EXPECT_EQ(body(p, "deposit")[4], "int object2{};");
    EXPECT_EQ(body(p, "deposit")[5], "int sut2{};");
}

TEST(TestPlannerFixture, TheFixtureDoesNotTakeTheNamesABodyIntroduces) {
    const Planned p = plan_fixture("class Foo { public: explicit Foo(int result, int sut, int object, int const_sut); void run(); };");
    EXPECT_EQ(fixture_members(fixture_of_suite(p)), (Lines{"int result2{};", "int sut2{};", "int object2{};", "int const_sut2{};", "Foo sut;"}));
    EXPECT_EQ(fixture_of_suite(p).initialisers, Lines{"sut(result2, sut2, object2, const_sut2)"});
}

TEST(TestPlannerFixture, ConstructorCasesBuildAnotherObjectCalledObject) {
    const Planned p = plan_fixture("class Foo { public: Foo(); explicit Foo(int n); };");
    EXPECT_EQ(body(p, "Constructs"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::Foo()", "blank", "// Arrange", "// Act", "Foo object;", "(void)object;",
        "// Assert", "// TODO(mt): check the state of object"}));
    EXPECT_EQ(body(p, "Constructs_int"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::Foo(int)", "blank", "// Arrange", "int n{};", "// Act", "Foo object(n);",
        "(void)object;", "// Assert", "// TODO(mt): check the state of object"}));
}

TEST(TestPlannerFixture, TheDestructorIsRunByTheFixture) {
    const Planned p = plan_fixture("class Foo { public: ~Foo(); };");
    EXPECT_EQ(body(p, "Destructs"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::~Foo()", "blank", "// Arrange", "// Act",
        "// the fixture destroys sut when the test ends", "// Assert", "// TODO(mt): check what the destructor releases"}));
}

TEST(TestPlannerFixture, AStaticMethodNeedsNoSutAndIsStillATestF) {
    const Planned p = plan_fixture("class Util { public: static int clamp(int v); };");
    EXPECT_EQ(body(p, "clamp")[3], "int v{};");
    EXPECT_EQ(body(p, "clamp")[5], "const int result = Util::clamp(v);");
}

TEST(TestPlannerFixture, WhereSutCannotBeBuiltTheMembersAreCommentsAndSoIsEveryCallOnIt) {
    const Planned p = plan_fixture("class Handle { public: explicit Handle(Mystery m); void close(); };");
    const ir::Fixture& f = fixture_of_suite(p);
    EXPECT_EQ(fixture_members(f), (Lines{"// TODO(mt): 'Handle' has no public constructor that can be called with default values: build sut by hand",
                                         "// Handle sut;"}));
    EXPECT_TRUE(f.initialisers.empty());
    EXPECT_EQ(body(p, "close"), (Lines{
        "placeholder: TODO(mt): write the test for Handle::close()", "blank", "// Arrange", "// Act", "// sut.close();", "// Assert",
        "// TODO(mt): check the effect of the call"}));
}

TEST(TestPlannerFixture, AnAbstractClassIsTheSame) {
    const Planned p = plan_fixture("class Shape { public: virtual double area() const = 0; };");
    EXPECT_EQ(fixture_members(fixture_of_suite(p)), (Lines{"// TODO(mt): 'Shape' is abstract: derive a test double and build that", "// Shape sut;"}));
    EXPECT_EQ(body(p, "area")[4], "// const Shape& const_sut = sut;");
}

TEST(TestPlannerFixture, AnArgumentThatCannotBeBuiltCommentsOutTheCallButNotTheFixture) {
    const Planned p = plan_fixture("class Foo { public: void f(int ok, Mystery bad); };");
    EXPECT_EQ(fixture_members(fixture_of_suite(p)), Lines{"Foo sut;"});
    EXPECT_EQ(body(p, "f"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::f(int, Mystery)", "blank", "// Arrange",
        "// TODO(mt): an argument of type 'Mystery' cannot be built here", "// int ok{};", "// Mystery bad{};", "// Act", "// sut.f(ok, bad);",
        "// Assert", "// TODO(mt): check the effect of the call"}));
}

TEST(TestPlannerFixture, AMoveInTheFixtureStillAsksForUtility) {
    const Planned p = plan_fixture("class Foo { public: explicit Foo(std::string&& s); void run(); };");
    EXPECT_EQ(fixture_of_suite(p).initialisers, Lines{"sut(std::move(s))"});
    ASSERT_EQ(p.plans[0].includes.size(), 3u);
    EXPECT_EQ(p.plans[0].includes[2].path, "utility");
}

TEST(TestPlannerFixture, EveryClassHasItsOwnAndTheyDoNotLeak) {
    const Planned p = plan_fixture("class A { public: explicit A(int a); void go(); };\nclass B { public: void go(); };");
    ASSERT_EQ(p.plans.size(), 2u);
    EXPECT_EQ(fixture_members(fixture_of_suite(p, 0)), (Lines{"int a{};", "A sut;"}));
    EXPECT_EQ(fixture_members(fixture_of_suite(p, 1)), Lines{"B sut;"});
    EXPECT_EQ(body(p, "go", 1)[3], "// Act") << "B's cases do not see A's members";
}

TEST(TestPlannerFixture, QtTestHasNoFixtureYetWhateverTheConfigurationSays) {
    core::Config config;
    config.gtest.fixture = true;
    const Planned p = plan_qt("class Foo { public: void run(); };", config);
    EXPECT_FALSE(suite(p).fixture.has_value());
    EXPECT_EQ(body(p, "run")[3], "Foo sut;");
}

// ---- mocks as arguments (gtest.mocks) -------------------------------------------------------------------------------------

namespace {

Planned plan_mocks_on(const std::string& header, core::Config config = {}, ir::Framework framework = ir::Framework::GTest) {
    config.gtest.mocks = true;
    return plan(header, std::move(config), "foo.h", framework);
}

const char* const kSensorAndLogger =
    "class Sensor { public: virtual ~Sensor() = default; virtual int read() = 0; };\n"
    "class Logger { public: virtual ~Logger() = default; virtual void log(const std::string& line) = 0; };\n";

std::vector<std::string> include_paths(const Planned& p, std::size_t plan_index) {
    std::vector<std::string> out;
    for (const ir::Include& i : p.plans.at(plan_index).includes) out.push_back(i.path);
    return out;
}

// The body of the first constructor case of the plan at `at`: the cases are named after their parameter types.
Lines constructs(const Planned& p, std::size_t at) {
    for (const ir::TestCase& c : suite(p, at).cases) {
        if (c.name.rfind("Constructs", 0) == 0) {
            Lines out;
            for (const Stmt& st : c.body) out.push_back(show(st));
            return out;
        }
    }
    ADD_FAILURE() << "no constructor case";
    return {};
}

// The plan of the class called `name`.
std::size_t plan_of(const Planned& p, const char* name) {
    for (std::size_t i = 0; i < p.plans.size(); ++i) {
        if (!p.plans[i].suites.empty() && p.plans[i].suites[0].target_class == name) return i;
    }
    ADD_FAILURE() << "no plan for " << name;
    return 0;
}

}  // namespace

TEST(TestPlannerMockArgs, AReferenceToAnInterfaceIsAMock) {
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) + "class Controller { public: explicit Controller(Sensor& sensor); };");
    const std::size_t at = plan_of(p, "Controller");
    EXPECT_EQ(constructs(p, at), (Lines{
        "placeholder: TODO(mt): write the test for Controller::Controller(Sensor&)", "blank", "// Arrange",
        "testing::NiceMock<MockSensor> sensor;", "// Act", "Controller sut(sensor);", "(void)sut;", "// Assert",
        "// TODO(mt): check the state of sut"}));
    EXPECT_EQ(include_paths(p, at), (Lines{"gtest/gtest.h", "foo.h", "mock_sensor.h"}));
}

TEST(TestPlannerMockArgs, ThatIsWhatMakesTheClassBuildable) {
    const std::string header = std::string(kSensorAndLogger) + "class Controller { public: explicit Controller(Sensor& sensor); int value() const; };";
    const Planned without = plan(header);
    EXPECT_EQ(body(without, "value", plan_of(without, "Controller"))[3],
              "// TODO(mt): 'Controller' has no public constructor that can be called with default values: build sut by hand")
        << "an interface cannot be built, so neither can the class";
    const Planned with = plan_mocks_on(header);
    EXPECT_EQ(body(with, "value", plan_of(with, "Controller"))[3], "testing::NiceMock<MockSensor> sensor;");
    EXPECT_EQ(include_paths(without, plan_of(without, "Controller")), (Lines{"gtest/gtest.h", "foo.h"}));
}

TEST(TestPlannerMockArgs, AnythingToAnInterfaceThatIsAPointerGetsItsAddress) {
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) +
                                    "class Controller { public: Controller(Sensor* a, const Logger* b, Sensor* const c); };");
    const Lines b = constructs(p, plan_of(p, "Controller"));
    EXPECT_EQ(b[3], "testing::NiceMock<MockSensor> a;");
    EXPECT_EQ(b[4], "testing::NiceMock<MockLogger> b;");
    EXPECT_EQ(b[5], "testing::NiceMock<MockSensor> c;");
    EXPECT_EQ(b[7], "Controller sut(&a, &b, &c);");
}

TEST(TestPlannerMockArgs, ASmartPointerOwnsATemporaryMock) {
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) +
                                    "class Controller { public: Controller(std::unique_ptr<Sensor> a, std::shared_ptr<Logger> b, const std::shared_ptr<Sensor>& c); };");
    const Lines b = constructs(p, plan_of(p, "Controller"));
    ASSERT_GE(b.size(), 6u);
    EXPECT_EQ(b[3], "// Act");
    EXPECT_EQ(b[4],
              "Controller sut(std::unique_ptr<Sensor>(new testing::NiceMock<MockSensor>), "
              "std::make_shared<testing::NiceMock<MockLogger>>(), std::make_shared<testing::NiceMock<MockSensor>>());");
}

TEST(TestPlannerMockArgs, TwoOfTheSameInterfaceDoNotShareAName) {
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) + "class Controller { public: Controller(Sensor& sensor, Sensor& sensor2, Sensor&); };");
    const Lines b = constructs(p, plan_of(p, "Controller"));
    EXPECT_EQ(b[3], "testing::NiceMock<MockSensor> sensor;");
    EXPECT_EQ(b[4], "testing::NiceMock<MockSensor> sensor2;");
    EXPECT_EQ(b[5], "testing::NiceMock<MockSensor> arg3;");
    EXPECT_EQ(b[7], "Controller sut(sensor, sensor2, arg3);");
}

TEST(TestPlannerMockArgs, TheArgumentsOfAMethodToo) {
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) +
                                    "class Controller { public: void attach(Sensor& sensor, int n); static void install(Logger* logger); };");
    EXPECT_EQ(body(p, "attach", plan_of(p, "Controller")), (Lines{
        "placeholder: TODO(mt): write the test for Controller::attach(Sensor&, int)", "blank", "// Arrange", "Controller sut;",
        "testing::NiceMock<MockSensor> sensor;", "int n{};", "// Act", "sut.attach(sensor, n);", "// Assert",
        "// TODO(mt): check the effect of the call"}));
    EXPECT_EQ(body(p, "install", plan_of(p, "Controller"))[3], "testing::NiceMock<MockLogger> logger;");
    EXPECT_EQ(include_paths(p, plan_of(p, "Controller")), (Lines{"gtest/gtest.h", "foo.h", "mock_logger.h", "mock_sensor.h"}));
}

TEST(TestPlannerMockArgs, ADestructorCaseBuildsItsSutWithMocksToo) {
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) + "class Controller { public: explicit Controller(Sensor& s); ~Controller(); };");
    EXPECT_EQ(body(p, "Destructs", plan_of(p, "Controller"))[3], "testing::NiceMock<MockSensor> s;");
    EXPECT_EQ(body(p, "Destructs", plan_of(p, "Controller"))[4], "Controller sut(s);");
}

TEST(TestPlannerMockArgs, ASmartPointerThatCannotTakeATemporaryIsNotAMock) {
    // `std::shared_ptr<Sensor>&` cannot bind the temporary the mock would be.
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) + "class User { public: explicit User(std::shared_ptr<Sensor>& s); };\n");
    const Lines b = constructs(p, plan_of(p, "User"));
    EXPECT_EQ(std::count_if(b.begin(), b.end(), [](const std::string& l) { return l.find("NiceMock") != std::string::npos; }), 0);
}

TEST(TestPlannerMockArgs, ABodyDoesNotTakeTheNameOfAMockTheFixtureOwns) {
    core::Config config;
    config.gtest.fixture = true;
    const Planned p = plan_mocks_on(
        std::string(kSensorAndLogger) + "class Controller { public: explicit Controller(Sensor& s); void set(int s); };", config);
    EXPECT_EQ(fixture_members(fixture_of_suite(p, plan_of(p, "Controller"))), (Lines{"testing::NiceMock<MockSensor> s;", "Controller sut;"}));
    Lines set;
    for (const ir::TestCase& c : suite(p, plan_of(p, "Controller")).cases) {
        if (c.target.find("::set(") != std::string::npos) {
            for (const Stmt& st : c.body) set.push_back(show(st));
        }
    }
    EXPECT_NE(std::find(set.begin(), set.end(), "int s2{};"), set.end()) << "a local called s would shadow the member";
}

TEST(TestPlannerMockArgs, TheMockHeaderIsALocalInclude) {
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) + "class User { public: explicit User(Sensor& s); };\n");
    for (const ir::Include& i : p.plans.at(plan_of(p, "User")).includes) {
        EXPECT_EQ(i.is_system, i.path == "gtest/gtest.h") << i.path;
    }
}

TEST(TestPlannerMockArgs, TheMocksOfOneClassAreNotIncludedByTheNext) {
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) +
                                    "class First { public: explicit First(Sensor& s); };\nclass Second { public: Second(); void run(); };\n");
    EXPECT_EQ(include_paths(p, plan_of(p, "First")), (Lines{"gtest/gtest.h", "foo.h", "mock_sensor.h"}));
    EXPECT_EQ(include_paths(p, plan_of(p, "Second")), (Lines{"gtest/gtest.h", "foo.h"}));
}

TEST(TestPlannerMockArgs, AFixtureOwnsTheMocksItsSutNeeds) {
    core::Config config;
    config.gtest.fixture = true;
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) + "class Controller { public: explicit Controller(Sensor& s); int value() const; };", config);
    const ir::Fixture& f = fixture_of_suite(p, plan_of(p, "Controller"));
    EXPECT_EQ(fixture_members(f), (Lines{"testing::NiceMock<MockSensor> s;", "Controller sut;"}));
    EXPECT_EQ(f.initialisers, Lines{"sut(s)"});
    EXPECT_EQ(include_paths(p, plan_of(p, "Controller")), (Lines{"gtest/gtest.h", "foo.h", "mock_sensor.h"}));
}

TEST(TestPlannerMockArgs, TheConstructorTheClassDoesNotNeedIsNotIncluded) {
    // Controller() is the simplest constructor: the interface of the other is never built, so its mock is not included.
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) + "class Controller { public: Controller(); explicit Controller(Sensor& s); };");
    EXPECT_EQ(include_paths(p, plan_of(p, "Controller")).size(), 3u) << "the constructor case that takes the mock includes it";
    const Planned only = plan_mocks_on(std::string(kSensorAndLogger) + "class Controller { public: Controller(); void run(); };");
    EXPECT_EQ(include_paths(only, plan_of(only, "Controller")), (Lines{"gtest/gtest.h", "foo.h"}));
}

TEST(TestPlannerMockArgs, ADifferentNamespaceNamesTheMockInFull) {
    const Planned p = plan_mocks_on(
        "namespace hw { class Sensor { public: virtual int read() = 0; }; }\n"
        "namespace app { class Controller { public: explicit Controller(hw::Sensor& s); }; }\n");
    EXPECT_EQ(constructs(p, plan_of(p, "Controller"))[3], "testing::NiceMock<hw::MockSensor> s;");
}

TEST(TestPlannerMockArgs, TheSameNamespaceNeedsNoQualifier) {
    const Planned p = plan_mocks_on(
        "namespace hw { class Sensor { public: virtual int read() = 0; };\n"
        " class Controller { public: explicit Controller(Sensor& s); }; }\n");
    EXPECT_EQ(constructs(p, plan_of(p, "Controller"))[3], "testing::NiceMock<MockSensor> s;");
}

TEST(TestPlannerMockArgs, TwoInterfacesOfOneNameTheClassOwnNamespaceWins) {
    const Planned p = plan_mocks_on(
        "namespace a { class Sensor { public: virtual int read() = 0; }; class User { public: explicit User(Sensor& s); }; }\n"
        "namespace b { class Sensor { public: virtual int read() = 0; }; }\n");
    EXPECT_EQ(constructs(p, plan_of(p, "User"))[3], "testing::NiceMock<MockSensor> s;");
}

TEST(TestPlannerMockArgs, TwoInterfacesOfOneNameAndNeitherIsTheClassOwnMeansNone) {
    const Planned p = plan_mocks_on(
        "namespace a { class Sensor { public: virtual int read() = 0; }; }\n"
        "namespace b { class Sensor { public: virtual int read() = 0; }; }\n"
        "namespace c { class User { public: explicit User(Sensor& s); }; }\n");
    EXPECT_EQ(constructs(p, plan_of(p, "User"))[3], "// TODO(mt): an argument of type 'Sensor' cannot be built here");
}

TEST(TestPlannerMockArgs, AMockThatIsStillAbstractIsNotUsed) {
    const Planned p = plan_mocks_on("class Sensor { public: virtual bool operator==(const Sensor&) const = 0; virtual int read() = 0; };\n"
                                    "class User { public: explicit User(Sensor& s); };\n");
    EXPECT_EQ(constructs(p, plan_of(p, "User"))[3], "// TODO(mt): an argument of type 'Sensor' cannot be built here");
}

TEST(TestPlannerMockArgs, WhatIsNotAnInterfaceOrNotMockableByItsShapeIsAsBefore) {
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) +
                                    "class Plain { public: int a; };\n"
                                    "class User { public: User(Plain& a, Sensor&& b, Sensor** c, std::vector<Sensor*> d); };\n");
    const Lines b = constructs(p, plan_of(p, "User"));
    ASSERT_GE(b.size(), 8u);
    EXPECT_EQ(b[3], "// TODO(mt): an argument of type 'Sensor' cannot be built here") << "the first one that is not Plain";
    for (const std::string& line : b) EXPECT_EQ(line.find("NiceMock"), std::string::npos) << line;
}

TEST(TestPlannerMockArgs, ByValueIsNotAnInterfaceEither) {
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) + "class User { public: explicit User(Sensor s); };\n");
    EXPECT_EQ(constructs(p, plan_of(p, "User"))[3], "// TODO(mt): an argument of type 'Sensor' cannot be built here");
}

TEST(TestPlannerMockArgs, QtTestHasNoMocks) {
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) + "class User { public: explicit User(Sensor& s); };\n", {}, ir::Framework::QTest);
    EXPECT_EQ(constructs(p, plan_of(p, "User"))[3], "// TODO(mt): an argument of type 'Sensor' cannot be built here");
}

TEST(TestPlannerMockArgs, TheMockFileOfAnotherNamePatternIsWhatIsIncluded) {
    core::Config config;
    config.naming.mock_file = "mocks/{stem}_mock.h";
    config.naming.mock_class = "{Class}Mock";
    const Planned p = plan_mocks_on(std::string(kSensorAndLogger) + "class User { public: explicit User(Sensor& s); };\n", config);
    EXPECT_EQ(constructs(p, plan_of(p, "User"))[3], "testing::NiceMock<SensorMock> s;");
    EXPECT_EQ(include_paths(p, plan_of(p, "User")), (Lines{"gtest/gtest.h", "foo.h", "mocks/sensor_mock.h"}));
}

TEST(TestPlannerMockArgs, PlanningTheTestsSaysNothingTheMocksDoNot) {
    // The mocks are planned to know what an argument is built with; their notes are said once, by whoever writes them.
    const Planned p = plan_mocks_on("template <typename T> class Sensor { public: virtual T read() = 0; };\nclass User { public: User(); };\n");
    for (const core::Diagnostic& d : p.diagnostics.all()) EXPECT_EQ(d.message.find("no mock is generated"), std::string::npos) << d.message;
}

// ---- cases over a table of arguments (gtest.parameterised, qtest.data_driven) ---------------------------------------------

namespace {

Planned plan_table(const std::string& header, core::Config config = {}, ir::Framework framework = ir::Framework::GTest) {
    config.gtest.parameterised = true;
    config.qtest.data_driven = true;
    return plan(header, std::move(config), "foo.h", framework);
}

// The case of the method called `method` (its name is made of its parameter types, which these tests do not care about).
const ir::TestCase& by(const Planned& p, const char* method, std::size_t plan_index = 0) {
    static const ir::TestCase none;
    for (const ir::TestCase& c : suite(p, plan_index).cases) {
        if (c.target.find(std::string("::") + method + "(") != std::string::npos) return c;
    }
    ADD_FAILURE() << "no case for " << method;
    return none;
}

Lines body_by(const Planned& p, const char* method, std::size_t plan_index = 0) {
    Lines out;
    for (const Stmt& st : by(p, method, plan_index).body) out.push_back(show(st));
    return out;
}

std::vector<std::string> column_names(const ir::TestCase& c) {
    std::vector<std::string> out;
    if (c.data) {
        for (const ir::Column& k : c.data->columns) out.push_back(k.type.spelling + " " + k.name);
    }
    return out;
}

}  // namespace

TEST(TestPlannerArguments, AnArgumentThatCannotBeCopiedIsMoved) {
    const Planned p = plan(
        "class Handle { public: Handle(); Handle(const Handle&) = delete; };\n"
        "class Foo { public: void own(std::unique_ptr<int> p); void use(Handle h); void copy(std::string s); void borrow(const std::unique_ptr<int>& p); };\n");
    EXPECT_EQ(body_by(p, "own", 1)[4], "std::unique_ptr<int> p{};");
    EXPECT_EQ(body_by(p, "own", 1)[6], "sut.own(std::move(p));");
    EXPECT_EQ(body_by(p, "use", 1)[6], "sut.use(std::move(h));");
    EXPECT_EQ(body_by(p, "copy", 1)[6], "sut.copy(s);") << "a string is copied";
    EXPECT_EQ(body_by(p, "borrow", 1)[6], "sut.borrow(p);") << "a reference is not a copy";
    EXPECT_EQ(p.plans[1].includes.size(), 3u) << "std::move needs <utility>";
}

TEST(TestPlannerTable, NotUnlessAskedFor) {
    const Planned p = plan("class Foo { public: int add(int a, int b); };");
    EXPECT_FALSE(by(p, "add").data.has_value());
    EXPECT_EQ(body_by(p, "add")[3], "Foo sut;");
    EXPECT_EQ(body_by(p, "add")[4], "int a{};");
}

TEST(TestPlannerTable, ATableDoesNotLeakIntoTheNextCase) {
    const Planned p = plan_table("class Foo { public: int add(int a, int b); int none(); void set(int n); };");
    EXPECT_TRUE(by(p, "add").data.has_value());
    EXPECT_FALSE(by(p, "none").data.has_value()) << "the table was moved to add's case";
    EXPECT_TRUE(by(p, "set").data.has_value());
}

TEST(TestPlannerTable, AReferenceToSomethingThatCannotBeCopiedIsNotAColumnEither) {
    const Planned p = plan_table(
        "class Handle { public: Handle(); Handle(const Handle&) = delete; };\n"
        "class Foo { public: void borrow(const std::unique_ptr<int>& p); void lend(Handle& h); void ok(int n); };\n");
    EXPECT_FALSE(by(p, "borrow", 1).data.has_value()) << "the column would be a unique_ptr";
    EXPECT_FALSE(by(p, "lend", 1).data.has_value());
    EXPECT_TRUE(by(p, "ok", 1).data.has_value());
}

TEST(TestPlannerTable, AMethodWithArgumentsRunsOverAnyTable) {
    const Planned p = plan_table("class Foo { public: int add(int a, int b) const; };");
    const ir::TestCase& c = by(p, "add");
    ASSERT_TRUE(c.data.has_value());
    EXPECT_EQ(column_names(c), (Lines{"int a", "int b"}));
    EXPECT_EQ(body_by(p, "add"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::add(int, int) const", "blank", "// Arrange", "Foo sut;", "data", "// Act",
        "const Foo& const_sut = sut;", "const int result = const_sut.add(a, b);", "(void)result;", "// Assert",
        "// TODO(mt): compare result with the expected value"}));
}

TEST(TestPlannerTable, TheColumnsAreTheLocalsWithoutReferenceOrConst) {
    const Planned p = plan_table("class Foo { public: void set(const std::string& name, const char* unit, int& out, double* p); };");
    EXPECT_EQ(column_names(by(p, "set")),
              (Lines{"std::string name", "const char* unit", "int out", "double* p"}));
}

TEST(TestPlannerTable, WhatTakesNoArgumentsOrIsNotACallIsAsBefore) {
    const Planned p = plan_table("class Foo { public: Foo(int a); ~Foo(); int size() const; static int twice(int x); };");
    EXPECT_FALSE(by(p, "Foo").data.has_value());
    EXPECT_FALSE(by(p, "~Foo").data.has_value());
    EXPECT_FALSE(by(p, "size").data.has_value());
    EXPECT_TRUE(by(p, "twice").data.has_value()) << "a static method too";
}

TEST(TestPlannerTable, ArgumentsThatCannotBeColumnsKeepTheirLocals) {
    const Planned p = plan_table(
        "#include <memory>\n"
        "class Foo { public:\n"
        "    void take(std::string&& s);\n"
        "    void own(std::unique_ptr<int> p);\n"
        "    void fill(int values[3]);\n"
        "};\n");
    EXPECT_FALSE(by(p, "take").data.has_value()) << "an rvalue reference is moved from a local";
    EXPECT_FALSE(by(p, "own").data.has_value()) << "a row is copied";
    EXPECT_EQ(body_by(p, "take")[4], "std::string s{};");
}

TEST(TestPlannerTable, ATypeThatDeletesItsCopyIsNotAColumn) {
    const Planned p = plan_table(
        "class Handle { public: Handle(); Handle(const Handle&) = delete; };\n"
        "class Foo { public: void use(Handle h); void ok(int a); };\n");
    EXPECT_FALSE(by(p, "use", 1).data.has_value());
    EXPECT_TRUE(by(p, "ok", 1).data.has_value());
}

TEST(TestPlannerTable, AnArgumentThatCannotBeBuiltKeepsTheCommentedCall) {
    const Planned p = plan_table("class Foo { public: void run(Mystery m); };");
    EXPECT_FALSE(by(p, "run").data.has_value());
    const Lines b = body_by(p, "run");
    EXPECT_NE(std::find(b.begin(), b.end(), "// TODO(mt): an argument of type 'Mystery' cannot be built here"), b.end());
    EXPECT_NE(std::find(b.begin(), b.end(), "// Mystery m{};"), b.end());
}

TEST(TestPlannerTable, AMockArgumentIsNotAColumn) {
    core::Config config;
    config.gtest.mocks = true;
    const Planned p = plan_table("class Sensor { public: virtual int read() = 0; };\nclass Foo { public: void attach(Sensor& s, int n); };\n", config);
    EXPECT_FALSE(by(p, "attach", 1).data.has_value()) << "a mock cannot be copied out of a table";
}

TEST(TestPlannerTable, AFixtureKeepsItsSutAndTheTableIsTheArguments) {
    core::Config config;
    config.gtest.fixture = true;
    const Planned p = plan_table("class Foo { public: void set(int n); };", config);
    EXPECT_TRUE(by(p, "set").data.has_value());
    EXPECT_EQ(body_by(p, "set"), (Lines{
        "placeholder: TODO(mt): write the test for Foo::set(int)", "blank", "// Arrange", "data", "// Act", "sut.set(n);", "// Assert",
        "// TODO(mt): check the effect of the call"}));
    EXPECT_TRUE(suite(p).fixture.has_value());
}

TEST(TestPlannerTable, GoogleTestKeepsTheNameParamForItself) {
    const Planned p = plan_table("class Foo { public: void set(int param, int sut, int result); };");
    EXPECT_EQ(column_names(by(p, "set")), (Lines{"int param2", "int sut2", "int result2"}));
}

TEST(TestPlannerTable, QtTestHasNoSuchNameToKeep) {
    const Planned p = plan_table("class Foo { public: void set(int param); };", {}, ir::Framework::QTest);
    EXPECT_EQ(column_names(by(p, "set")), Lines{"int param"});
}

TEST(TestPlannerTable, TheFrameworkReadsItsOwnOption) {
    core::Config only_gtest;
    only_gtest.gtest.parameterised = true;
    EXPECT_TRUE(by(plan("class Foo { public: void f(int a); };", only_gtest), "f").data.has_value());
    EXPECT_FALSE(by(plan_qt("class Foo { public: void f(int a); };", only_gtest), "f").data.has_value());

    core::Config only_qtest;
    only_qtest.qtest.data_driven = true;
    EXPECT_FALSE(by(plan("class Foo { public: void f(int a); };", only_qtest), "f").data.has_value());
    EXPECT_TRUE(by(plan_qt("class Foo { public: void f(int a); };", only_qtest), "f").data.has_value());
}

TEST(TestPlannerTable, QtTestCannotSpellEveryTypeInQFetch) {
    const Planned p = plan_table(
        "class Foo { public:\n"
        "    void map(std::map<int, int> m);\n"
        "    void fine(std::vector<int> v);\n"
        "};\n", {}, ir::Framework::QTest);
    EXPECT_FALSE(by(p, "map").data.has_value()) << "the comma ends the macro's first argument";
    EXPECT_TRUE(by(p, "fine").data.has_value());
}

TEST(TestPlannerTable, QtTestDoesNotTakeConstCharPointerAsAColumn) {
    const Planned qt = plan_table("class Foo { public: void a(const char* s); void b(char* s); void c(int n, const char* s); };", {}, ir::Framework::QTest);
    EXPECT_FALSE(by(qt, "a").data.has_value()) << "QTest::addColumn<const char*> is a static_assert";
    EXPECT_TRUE(by(qt, "b").data.has_value());
    EXPECT_FALSE(by(qt, "c").data.has_value()) << "one column that cannot be one keeps all the locals";
    const Planned gt = plan_table("class Foo { public: void a(const char* s); };");
    EXPECT_TRUE(by(gt, "a").data.has_value());
}

TEST(TestPlannerTable, GoogleTestCanSpellAComma) {
    const Planned p = plan_table("class Foo { public: void map(std::map<int, int> m); };");
    EXPECT_TRUE(by(p, "map").data.has_value());
}

TEST(TestPlannerTable, DefaultedArgumentsAreLeftOutOfTheTable) {
    const Planned p = plan_table("class Foo { public: void f(int a, int b = 3); };");
    EXPECT_EQ(column_names(by(p, "f")), Lines{"int a"});
}

// ---- class templates (template_instantiations) --------------------------------------------------------------------------------

namespace {

Planned plan_instances(const std::string& header, std::map<std::string, std::vector<std::string>> instantiations,
                       core::Config config = {}, ir::Framework framework = ir::Framework::GTest) {
    config.template_instantiations = std::move(instantiations);
    return plan(header, std::move(config), "foo.h", framework);
}

const char* const kBox = "namespace acme { template <typename T, int N = 4> class Box { public: Box(); explicit Box(T v); T get(int i) const; static int capacity(); }; }\n";

}  // namespace

TEST(TestPlannerTemplates, OnePlanPerInstantiationWithTheNamesOfItsOwn) {
    const Planned p = plan_instances(kBox, {{"acme::Box", {"int", "std::string, 8"}}});
    ASSERT_EQ(p.plans.size(), 2u);
    EXPECT_EQ(p.plans[0].file, "box_int_test.cpp");
    EXPECT_EQ(p.plans[1].file, "box_std_string_8_test.cpp");
    EXPECT_EQ(suite(p, 0).name, "Box_intTest");
    EXPECT_EQ(suite(p, 1).name, "Box_std_string_8Test");
    EXPECT_EQ(suite(p, 0).target_class, "Box<int>");
    EXPECT_EQ(suite(p, 0).ns, std::vector<std::string>{"acme"});
}

TEST(TestPlannerTemplates, TheTestsNameTheInstantiation) {
    const Planned p = plan_instances(kBox, {{"acme::Box", {"std::string, 8"}}});
    EXPECT_EQ(case_names(p), (Lines{"Constructs", "Constructs_std_string", "get", "capacity"}));
    EXPECT_EQ(body(p, "get"), (Lines{
        "placeholder: TODO(mt): write the test for acme::Box<std::string, 8>::get(int) const", "blank", "// Arrange",
        "Box<std::string, 8> sut;", "int i{};", "// Act", "const Box<std::string, 8>& const_sut = sut;",
        "const std::string result = const_sut.get(i);", "(void)result;", "// Assert", "// TODO(mt): compare result with the expected value"}));
    EXPECT_EQ(body(p, "capacity")[4], "const int result = Box<std::string, 8>::capacity();");
    EXPECT_EQ(body(p, "Constructs_std_string")[3], "std::string v{};");
    EXPECT_EQ(body(p, "Constructs_std_string")[5], "Box<std::string, 8> sut(v);");
}

TEST(TestPlannerTemplates, AnInstantiationWithoutADefaultConstructorIsBuiltWithItsOtherConstructor) {
    const Planned p = plan_instances("template <typename T> class Box { public: explicit Box(T v); T get() const; };\n", {{"Box", {"int"}}});
    EXPECT_EQ(body(p, "get")[3], "int v{};");
    EXPECT_EQ(body(p, "get")[4], "Box<int> sut(v);");
}

TEST(TestPlannerTemplates, ADefaultConstructorIsFoundForTheInstantiation) {
    // `Box<int> sut();` would declare a function: the class named Box<int> is found, and is default constructible.
    const Planned p = plan_instances("template <typename T> class Box { public: Box(); T get() const; };\n", {{"Box", {"int"}}});
    EXPECT_EQ(body(p, "get")[3], "Box<int> sut;");
}

TEST(TestPlannerTemplates, AnotherTemplateOfTheSameHeaderStaysSkippedWithAnAdvice) {
    const Planned p = plan_instances(
        "template <typename T> class Box { public: T get() const; };\n"
        "template <typename T> class Other { public: T get() const; };\n"
        "class Plain { public: int size() const; };\n",
        {{"Box", {"int"}}});
    ASSERT_EQ(p.plans.size(), 2u);
    ASSERT_EQ(p.diagnostics.all().size(), 1u);
    EXPECT_EQ(p.diagnostics.all()[0].message,
              "the class template 'Other' is skipped: list the instantiations to test in template_instantiations (\"Other\": [\"int\"])");
}

TEST(TestPlannerTemplates, TheConfigurationErrorsAreWarningsOnTheRun) {
    const Planned p = plan_instances("template <typename T> class Box { public: T get() const; };\n", {{"Box", {"int, 3", "int"}}});
    EXPECT_EQ(p.plans.size(), 1u);
    bool said = false;
    for (const core::Diagnostic& d : p.diagnostics.all()) said = said || d.code == core::code::config;
    EXPECT_TRUE(said);
}

TEST(TestPlannerTemplates, QtTestGetsAClassAndAFileForEach) {
    const Planned p = plan_instances("template <typename T> class Box { public: T get() const; };\n", {{"Box", {"int", "char"}}}, {}, ir::Framework::QTest);
    ASSERT_EQ(p.plans.size(), 2u);
    EXPECT_EQ(p.plans[0].file, "tst_box_int.cpp");
    EXPECT_EQ(suite(p, 0).name, "TestBox_int");
    EXPECT_EQ(suite(p, 1).name, "TestBox_char");
}

TEST(TestPlannerTemplates, TheNamingPatternsStillApply) {
    core::Config config;
    config.naming.suite = "{Class}Spec";
    config.naming.gtest_file = "specs/{stem}.cpp";
    const Planned p = plan_instances("template <typename T> class Box { public: T get() const; };\n", {{"Box", {"int"}}}, config);
    EXPECT_EQ(p.plans[0].file, "specs/box_int.cpp");
    EXPECT_EQ(suite(p).name, "Box_intSpec");
}

TEST(TestPlannerTemplates, AFixtureOwnsTheInstantiation) {
    core::Config config;
    config.gtest.fixture = true;
    const Planned p = plan_instances("template <typename T> class Box { public: T get() const; };\n", {{"Box", {"int"}}}, config);
    EXPECT_EQ(fixture_members(fixture_of_suite(p)), Lines{"Box<int> sut;"});
    EXPECT_EQ(fixture_of_suite(p).name, "Box_intTest");
}

TEST(TestPlannerTemplates, NoMockIsMadeOfAnInstantiation) {
    // A mock called MockSensor<int> would not be a name. (The interface that takes the mock as an
    // argument is a class of the header; an instantiation is not.)
    core::Diagnostics d;
    const ir::Model model = frontend::read_header({"foo.h", "template <typename T> class Sensor { public: virtual T read() = 0; };\n"}, "foo.h", {}, d);
    core::Config config;
    config.gtest.mocks = true;
    config.template_instantiations = {{"Sensor", {"int"}}};
    backend::PlannerOptions options;
    options.config = config;
    const ir::Model instantiated = backend::instantiate_templates(model, config, d);
    ASSERT_EQ(instantiated.classes.size(), 1u);
    EXPECT_EQ(instantiated.classes[0].name, "Sensor<int>");
    EXPECT_TRUE(backend::plan_mocks(instantiated, options, d).empty());
}

}  // namespace
