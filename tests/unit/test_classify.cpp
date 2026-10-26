// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// "Facts in, targets out": each test reads a fragment of a test file with the
// real frontend, classifies it, and checks which classes and functions the
// tests demand and where they go.

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "mirrortest/frontend/classify.h"
#include "mirrortest/frontend/code_frontend.h"
#include "mirrortest/frontend/test_frontend.h"

using namespace mt;
using frontend::Construction;
using frontend::FactRef;
using frontend::UseKind;

namespace {

struct Outcome {
    frontend::Classified result;
    core::Diagnostics    diagnostics;     // classification's own; the frontend's are dropped
};

Outcome classify(const std::string& text, frontend::ClassifyOptions options = {}) {
    core::Diagnostics ignored;
    frontend::TestFileFacts facts = frontend::read_test_file({"t.cpp", text}, {}, ignored);
    Outcome r;
    r.result = frontend::classify(std::move(facts), options, r.diagnostics);
    return r;
}

Outcome body(const std::string& statements, const std::string& prelude = "", frontend::ClassifyOptions options = {}) {
    return classify("#include <gtest/gtest.h>\n" + prelude + "\nTEST(S, N) {\n" + statements + "\n}\n", std::move(options));
}

std::vector<std::string> class_names(const Outcome& r) {
    std::vector<std::string> out;
    for (const frontend::ClassTarget& c : r.result.classes) out.push_back(c.qualified());
    return out;
}

std::vector<std::string> function_names(const Outcome& r) {
    std::vector<std::string> out;
    for (const frontend::FunctionTarget& f : r.result.functions) out.push_back(f.qualified());
    return out;
}

const frontend::ClassTarget& the_class(const Outcome& r, const char* qualified) {
    static const frontend::ClassTarget none;
    for (const frontend::ClassTarget& c : r.result.classes) {
        if (c.qualified() == qualified) return c;
    }
    ADD_FAILURE() << "no class " << qualified;
    return none;
}

const frontend::FunctionTarget& the_function(const Outcome& r, const char* qualified) {
    static const frontend::FunctionTarget none;
    for (const frontend::FunctionTarget& f : r.result.functions) {
        if (f.qualified() == qualified) return f;
    }
    ADD_FAILURE() << "no function " << qualified;
    return none;
}

std::vector<std::string> codes(const Outcome& r, core::Severity severity) {
    std::vector<std::string> out;
    for (const core::Diagnostic& d : r.diagnostics.sorted()) {
        if (d.severity == severity) out.push_back(d.code);
    }
    return out;
}
std::vector<std::string> warnings(const Outcome& r) { return codes(r, core::Severity::Warning); }
std::vector<std::string> notes(const Outcome& r) { return codes(r, core::Severity::Note); }

frontend::ClassifyOptions with_config(void (*edit)(core::Config&)) {
    frontend::ClassifyOptions options;
    edit(options.config);
    return options;
}

// A header the test "includes", as the code frontend would have read it.
ir::Model header(const char* text) {
    core::Diagnostics diag;
    return frontend::read_header({"known.h", text}, "known.h", {}, diag);
}

// ---- which names are classes ---------------------------------------------------------------------------

TEST(ClassifyClasses, ADeclaredTypeIsAClassToGenerate) {
    const Outcome r = body("Foo foo(3);\nfoo.run();\nEXPECT_EQ(foo.size(), 3u);\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Foo"});
    const frontend::ClassTarget& foo = the_class(r, "Foo");
    EXPECT_EQ(foo.method_calls.size(), 2u);
    EXPECT_EQ(foo.constructions.size(), 1u);
    EXPECT_EQ(foo.first_use.line, 4u);
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(ClassifyClasses, FieldAccessesAndNewAreEvidenceToo) {
    const Outcome r = body("Foo* p = new Foo(1);\np->x = 5;\nBar bar;\nint n = bar.y;\n");
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"Foo", "Bar"}));
    EXPECT_EQ(the_class(r, "Foo").field_accesses.size(), 1u);
    EXPECT_EQ(the_class(r, "Foo").constructions.size(), 1u);
    EXPECT_EQ(the_class(r, "Bar").field_accesses.size(), 1u);
}

TEST(ClassifyClasses, ClassesComeInOrderOfFirstUseNotOfDiscovery) {
    // Zed is found through a static call, which is resolved after the types.
    const Outcome r = body("Zed::make();\nAlpha a;\nMid m;\n");
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"Zed", "Alpha", "Mid"}));
}

TEST(ClassifyClasses, TypesTheLanguageAndLibrariesProvideAreExternal) {
    const Outcome r = body(
        "std::string s;\nstd::vector<Foo> v;\nQString q;\nsize_t n = 0;\nuint8_t b = 0;\nqint64 big = 0;\n"
        "testing::Test* t = nullptr;\nstd::unique_ptr<Bar> u;\nQTest::something x;\nFILE* f = nullptr;\n");
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"Foo", "Bar"}));
}

TEST(ClassifyClasses, TheTestFilesOwnTypesAreExternal) {
    const Outcome r = classify(
        "enum class Mode { On, Off };\n"
        "struct Helper { int x; };\n"
        "using Vec = std::vector<Foo>;\n"
        "typedef unsigned long Word;\n"
        "class Fix : public ::testing::Test { protected: Helper h_; };\n"
        "TEST_F(Fix, T) { Mode m; Helper h; Vec v; Word w; Fix* f = nullptr; Real r; }\n");
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"Foo", "Real"}));
}

TEST(ClassifyClasses, TemplateParametersAreNotClasses) {
    const Outcome r = classify("template <typename T> void check(const T& x) { T copy = x; Foo f; }\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Foo"});
}

TEST(ClassifyClasses, UsingNamespaceStdMakesTheStandardNamesExternal) {
    const Outcome with = classify("using namespace std;\nTEST(S, N) { string s; vector<Foo> v; unique_ptr<Bar> p; Baz z; }\n");
    EXPECT_EQ(class_names(with), (std::vector<std::string>{"Foo", "Bar", "Baz"}));
    const Outcome without = body("string s;\nvector<Foo> v;\n");
    EXPECT_EQ(class_names(without), (std::vector<std::string>{"string", "vector", "Foo"}))
        << "without the using, `string` is just a class nobody provides";
}

TEST(ClassifyClasses, HeadersTheTestIncludesProvideTheirTypes) {
    frontend::ClassifyOptions options;
    options.known.push_back(header("namespace acme { class Known { public: int size() const; }; enum class Level { Low }; }\nclass Plain {};\n"));
    const Outcome r = body("Known k;\nacme::Known k2;\nLevel l;\nPlain p;\nNew n;\nother::Known x;\n", "", options);
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"New", "other::Known"}))
        << "other::Known is not acme::Known";
}

TEST(ClassifyClasses, TheConfigurationNamesExternalTypes) {
    frontend::ClassifyOptions options = with_config([](core::Config& c) { c.external_types = {"Legacy", "vendor::Driver"}; });
    const Outcome r = body("Legacy a;\nvendor::Driver d;\nDriver d2;\nacme::Legacy l;\nOwn o;\n", "", options);
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"Own"}));
}

TEST(ClassifyClasses, TypesUsedOnlyAsTypesStillNeedToExist) {
    const Outcome r = body("std::vector<Foo> v;\nFoo* p = nullptr;\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Foo"});
    const frontend::ClassTarget& foo = the_class(r, "Foo");
    EXPECT_TRUE(foo.method_calls.empty());
    EXPECT_TRUE(foo.constructions.empty());
}

TEST(ClassifyClasses, TemplateInstantiationsAreCollected) {
    const Outcome r = body("Box<int> a(1);\nBox<std::string> b;\nBox<int> c;\n");
    ASSERT_EQ(class_names(r), std::vector<std::string>{"Box"});
    EXPECT_EQ(the_class(r, "Box").instantiations, (std::vector<std::string>{"int", "std::string"}));
}

// ---- where classes go ---------------------------------------------------------------------------------------------

TEST(ClassifyNamespaces, AQualifiedNameIsAskingForThatNamespace) {
    const Outcome r = body("acme::detail::Foo f;\nacme::Bar b;\n");
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"acme::detail::Foo", "acme::Bar"}));
}

TEST(ClassifyNamespaces, TheSameClassWrittenWithAndWithoutItsNamespaceIsOneClass) {
    const Outcome r = body("Foo early;\nacme::Foo late;\nFoo again;\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"acme::Foo"});
    EXPECT_EQ(the_class(r, "acme::Foo").constructions.size(), 3u);
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(ClassifyNamespaces, UsingDeclarationSaysWhereAClassLives) {
    const Outcome r = classify("using acme::Foo;\nTEST(S, N) { Foo f; }\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"acme::Foo"});
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(ClassifyNamespaces, UsingDeclarationOfAForeignNameSaysNothing) {
    const Outcome r = classify("using std::string;\nusing testing::Test;\nTEST(S, N) { string s; Foo f; }\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Foo"});
}

TEST(ClassifyNamespaces, TwoNamespacesForOneNameAreTwoClassesAndTheBareUseGetsTheFirst) {
    const Outcome r = body("a::Foo x;\nb::Foo y;\nFoo z;\n");
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"a::Foo", "b::Foo"}));
    EXPECT_EQ(the_class(r, "a::Foo").constructions.size(), 2u);
    EXPECT_EQ(warnings(r), std::vector<std::string>{"mt-ambiguous-scope"});
}

TEST(ClassifyNamespaces, TestsInsideANamespaceUseIt) {
    const Outcome r = classify("namespace acme { namespace v2 {\nTEST(S, N) { Foo f; }\n} }\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"acme::v2::Foo"});
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(ClassifyNamespaces, OneUsingNamespaceIsUsedWithANote) {
    const Outcome r = classify("using namespace acme;\nTEST(S, N) { Foo f; Bar b; }\n");
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"acme::Foo", "acme::Bar"}));
    EXPECT_EQ(notes(r), (std::vector<std::string>{"mt-ambiguous-scope", "mt-ambiguous-scope"}));
    EXPECT_TRUE(warnings(r).empty());
}

TEST(ClassifyNamespaces, UsingNamespaceStdAndTestingDoNotCount) {
    const Outcome r = classify("using namespace std;\nusing namespace testing;\nusing namespace acme;\nTEST(S, N) { Foo f; }\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"acme::Foo"});
}

TEST(ClassifyNamespaces, SeveralUsingNamespacesMeanGlobalAndAWarning) {
    const Outcome r = classify("using namespace a;\nusing namespace b;\nTEST(S, N) { Foo f; }\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Foo"});
    EXPECT_EQ(warnings(r), std::vector<std::string>{"mt-ambiguous-scope"});
}

TEST(ClassifyNamespaces, TheConfiguredDefaultResolvesTheAmbiguityWithoutAWarning) {
    frontend::ClassifyOptions options = with_config([](core::Config& c) { c.default_namespace = "proj::core"; });
    const Outcome r = classify("using namespace a;\nusing namespace b;\nTEST(S, N) { Foo f; }\n", options);
    EXPECT_EQ(class_names(r), std::vector<std::string>{"proj::core::Foo"});
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(ClassifyNamespaces, TheConfiguredDefaultIsTheLastResort) {
    frontend::ClassifyOptions options = with_config([](core::Config& c) { c.default_namespace = "proj"; });
    EXPECT_EQ(class_names(body("Foo f;\n", "", options)), std::vector<std::string>{"proj::Foo"});
    EXPECT_EQ(class_names(classify("using namespace acme;\nTEST(S, N) { Foo f; }\n", options)),
              std::vector<std::string>{"acme::Foo"}) << "the file's own using wins over the default";
    EXPECT_EQ(class_names(classify("namespace t { TEST(S, N) { Foo f; } }\n", options)),
              std::vector<std::string>{"t::Foo"});
}

TEST(ClassifyNamespaces, AnnotationDecidesAnUnqualifiedClassButNotAQualifiedOne) {
    const Outcome r = classify(
        "// @mt: class Foo namespace ann::ns; class Bar namespace ann::ns\n"
        "using namespace acme;\nTEST(S, N) { Foo f; code::Bar b; }\n");
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"ann::ns::Foo", "code::Bar"}));
    EXPECT_TRUE(notes(r).empty()) << "the annotation settled it; the using was not consulted";
}

TEST(ClassifyNamespaces, NestedTypesOfAGeneratedClassAreReportedNotGenerated) {
    const Outcome r = body("Outer o;\nOuter::Inner i;\nOuter::Inner j;\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Outer"});
    EXPECT_EQ(warnings(r), std::vector<std::string>{"mt-unsupported"}) << "once, not once per mention";
}

TEST(ClassifyNamespaces, NestedTypesOfSomebodyElsesClassAreSomebodyElses) {
    frontend::ClassifyOptions options;
    options.known.push_back(header("class Known { public: struct Item {}; };\n"));
    const Outcome r = body("Known::Item i;\nstd::vector<int>::iterator it;\nQString::SectionFlag f;\n", "", options);
    EXPECT_TRUE(class_names(r).empty());
    EXPECT_TRUE(r.diagnostics.empty());
}

// ---- X::m() (R13) ---------------------------------------------------------------------------------------------------

TEST(ClassifyScope, StaticCallOfAKnownClass) {
    const Outcome r = body("Foo foo;\nFoo::create(1);\nEXPECT_EQ(Foo::count(), 2);\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Foo"});
    EXPECT_EQ(the_class(r, "Foo").static_calls.size(), 2u);
    EXPECT_TRUE(function_names(r).empty());
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(ClassifyScope, StaticCallAlsoMatchesTheNamespaceOfAQualifiedClass) {
    const Outcome r = body("acme::Foo foo;\nacme::Foo::create(1);\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"acme::Foo"});
    EXPECT_EQ(the_class(r, "acme::Foo").static_calls.size(), 1u);
}

TEST(ClassifyScope, ANamespaceTheFileShowsMakesItAFunction) {
    // 1: as the qualifier of a class; 2: as a namespace the tests sit in;
    // 3: in a using-directive. None of them warns.
    const Outcome r = classify(
        "using namespace util;\n"
        "namespace helpers { TEST(S, A) { helpers::run(1); } }\n"
        "TEST(S, B) { acme::Foo f; acme::build(2); util::go(); }\n");
    EXPECT_EQ(function_names(r), (std::vector<std::string>{"helpers::run", "acme::build", "util::go"}));
    EXPECT_TRUE(warnings(r).empty());
}

TEST(ClassifyScope, WithNoEvidenceCapitalisedMeansClassAndLowercaseMeansNamespace) {
    const Outcome r = body("Widget::make(1);\nutil::go(2);\nWidget::again();\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Widget"});
    EXPECT_EQ(the_class(r, "Widget").static_calls.size(), 2u);
    EXPECT_EQ(function_names(r), std::vector<std::string>{"util::go"});
    EXPECT_EQ(warnings(r), (std::vector<std::string>{"mt-ambiguous-scope", "mt-ambiguous-scope"}))
        << "the second Widget:: call is no longer a question";
}

TEST(ClassifyScope, ACallInTheScopeOfSomebodyElsesNameGeneratesNothing) {
    frontend::ClassifyOptions options;
    options.known.push_back(header("class Known { public: static int make(); };\nnamespace lib { int twice(int); }\n"));
    const Outcome r = classify(
        "class Fix : public ::testing::Test { public: static void Helper(); };\n"
        "TEST(S, N) {\n"
        "    std::sort(a, b); testing::Return(1); QTest::qWait(1); QString::number(3);\n"
        "    Known::make(); lib::twice(2); Fix::Helper(); ::testing::InitGoogleTest();\n"
        "}\n", options);
    EXPECT_TRUE(class_names(r).empty());
    EXPECT_TRUE(function_names(r).empty());
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(ClassifyScope, AFunctionOfANamespaceTheFileDeclaresIsNotGenerated) {
    const Outcome r = classify("namespace helpers { int twice(int x); }\nTEST(S, N) { helpers::twice(2); helpers::thrice(3); }\n");
    EXPECT_EQ(function_names(r), std::vector<std::string>{"helpers::thrice"});
    EXPECT_TRUE(warnings(r).empty());
}

TEST(ClassifyScope, ScopedFunctionsMergeAcrossCalls) {
    const Outcome r = classify("namespace acme { TEST(S, N) { acme::add(1, 2); acme::add(3, 4); acme::sub(1, 1); } }\n");
    EXPECT_EQ(function_names(r), (std::vector<std::string>{"acme::add", "acme::sub"}));
    EXPECT_EQ(the_function(r, "acme::add").scoped_calls.size(), 2u);
}

// ---- f(x) (7.8) -------------------------------------------------------------------------------------------------------------

TEST(ClassifyFree, AnUnqualifiedCallNobodyProvidesIsAFunctionToGenerate) {
    const Outcome r = body("EXPECT_EQ(add(1, 2), 3);\nint n = add(3, 4);\nreset();\n");
    EXPECT_EQ(function_names(r), (std::vector<std::string>{"add", "reset"}));
    EXPECT_EQ(the_function(r, "add").free_calls.size(), 2u);
}

TEST(ClassifyFree, MacrosLibcQtAndFrameworkCallsAreNot) {
    const Outcome r = body(
        "MY_CHECK(x);\nASSERT_VALID(y);\nstrlen(s);\nmemcpy(a, b, 3);\nint n = abs(-3);\nprintf(\"x\");\nassert(n);\n"
        "qDebug();\nqMax(1, 2);\nRecordProperty(\"k\", \"v\");\n");
    EXPECT_TRUE(function_names(r).empty());
}

TEST(ClassifyFree, TheConfigurationExtendsTheExceptions) {
    frontend::ClassifyOptions options = with_config([](core::Config& c) {
        c.ignore_macros = {"my_lowercase_macro"};
        c.external_functions = {"hal_read", "vendor::init"};
    });
    const Outcome r = body("my_lowercase_macro(1);\nhal_read(2);\ninit();\nown(3);\n", "", options);
    EXPECT_EQ(function_names(r), std::vector<std::string>{"own"});
}

TEST(ClassifyFree, StdFunctionsOnlyCountWithUsingNamespaceStd) {
    const Outcome with = classify("using namespace std;\nTEST(S, N) { swap(a, b); int m = max(1, 2); own(); }\n");
    EXPECT_EQ(function_names(with), std::vector<std::string>{"own"});
    const Outcome without = body("swap(a, b);\nint m = max(1, 2);\n");
    EXPECT_EQ(function_names(without), (std::vector<std::string>{"swap", "max"}));
}

TEST(ClassifyFree, FunctionsTheFileOrItsHeadersProvideAreNot) {
    frontend::ClassifyOptions options;
    options.known.push_back(header("int from_header(int);\nnamespace lib { void in_lib(); }\n"));
    const Outcome r = classify(
        "int local_prototype(int);\n"
        "static int local_definition(int x) { return x; }\n"
        "TEST(S, N) { local_prototype(1); local_definition(2); from_header(3); in_lib(); wanted(4); }\n", options);
    EXPECT_EQ(function_names(r), std::vector<std::string>{"wanted"});
}

TEST(ClassifyFree, AFixtureMayCallItsOwnAndItsBasesMethods) {
    frontend::ClassifyOptions options;
    options.known.push_back(header("class KnownBase : public ::testing::Test { protected: void Reset(); };\n"));
    const Outcome r = classify(
        "class Mid : public KnownBase { protected: void Prepare(); int count_ = 0; };\n"
        "class Fix : public Mid { protected: void Check(); };\n"
        "void Fix::Check() { Prepare(); Reset(); RecordProperty(\"k\", \"v\"); foreign(1); }\n"
        "TEST_F(Fix, T) { Check(); Prepare(); Reset(); other(2); }\n"
        "TEST(S, N) { Check(); }\n", options);
    EXPECT_EQ(function_names(r), (std::vector<std::string>{"foreign", "other", "Check"}));
    EXPECT_EQ(the_function(r, "Check").free_calls.size(), 1u) << "outside a fixture, Check() is just a function";
}

TEST(ClassifyFree, FreeFunctionsTakeTheNamespaceTheFileAlsoWritesForThem) {
    const Outcome r = body("acme::add(1, 2);\nadd(3, 4);\n");
    EXPECT_EQ(function_names(r), std::vector<std::string>{"acme::add"});
    EXPECT_EQ(the_function(r, "acme::add").free_calls.size(), 1u);
    EXPECT_EQ(the_function(r, "acme::add").scoped_calls.size(), 1u);
}

TEST(ClassifyFree, FreeFunctionsFollowTheSameNamespaceRulesAsClasses) {
    EXPECT_EQ(function_names(classify("namespace t { TEST(S, N) { add(1); } }\n")), std::vector<std::string>{"t::add"});
    EXPECT_EQ(function_names(classify("using namespace acme;\nTEST(S, N) { add(1); }\n")), std::vector<std::string>{"acme::add"});
    EXPECT_EQ(function_names(classify("using namespace a;\nusing namespace b;\nTEST(S, N) { add(1); }\n")),
              std::vector<std::string>{"add"});
    frontend::ClassifyOptions options = with_config([](core::Config& c) { c.default_namespace = "proj"; });
    EXPECT_EQ(function_names(body("add(1);\n", "", options)), std::vector<std::string>{"proj::add"});
}

TEST(ClassifyFree, ACallThatNamesAClassIsAConstruction) {
    const Outcome r = body("Foo foo;\nprocess(Foo(3, 4));\nBar(5);\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Foo"});
    EXPECT_EQ(function_names(r), (std::vector<std::string>{"process", "Bar"})) << "Bar has no evidence of being a class";
    const frontend::ClassTarget& foo = the_class(r, "Foo");
    ASSERT_EQ(foo.constructions.size(), 2u);
    const Construction& cast = r.result.facts.constructions[foo.constructions[1]];
    EXPECT_EQ(cast.form, Construction::Form::Functional);
    EXPECT_EQ(cast.args.size(), 2u);
    EXPECT_EQ(cast.type.name, "Foo");
}

TEST(ClassifyFree, AQualifiedCallThatNamesAClassIsAConstructionToo) {
    const Outcome r = body("acme::Foo foo;\nprocess(acme::Foo(3, 4));\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"acme::Foo"});
    EXPECT_EQ(function_names(r), std::vector<std::string>{"process"}) << "not a function Foo in the namespace acme";
    const frontend::ClassTarget& foo = the_class(r, "acme::Foo");
    ASSERT_EQ(foo.constructions.size(), 2u);
    const Construction& cast = r.result.facts.constructions[foo.constructions[1]];
    EXPECT_EQ(cast.form, Construction::Form::Functional);
    EXPECT_EQ(cast.args.size(), 2u);
    EXPECT_EQ(cast.type.qualified(), "acme::Foo");
    ASSERT_EQ(r.result.scoped_casts.size(), 1u);
    EXPECT_EQ(r.result.scoped_casts.begin()->second.qualified(), "acme::Foo");
}

TEST(ClassifyFree, AQualifiedCallIsAFunctionWhenNothingSaysItsNameIsAClass) {
    const Outcome r = body("process(acme::Foo(3));\n");
    EXPECT_EQ(function_names(r), (std::vector<std::string>{"process", "acme::Foo"}));
    EXPECT_TRUE(r.result.scoped_casts.empty());
}

TEST(ClassifyFree, TheNamespaceHasToMatchForAQualifiedCast) {
    const Outcome r = body("acme::Foo foo;\nuse(other::Foo(3));\n");
    EXPECT_TRUE(r.result.scoped_casts.empty());
    EXPECT_EQ(function_names(r), (std::vector<std::string>{"use", "other::Foo"}));
}

TEST(ClassifyFree, CastOfTellsAFreeCastFromAScopedOneFromNeither) {
    const Outcome r = body("acme::Foo foo;\nFoo local;\nuse(acme::Foo(1));\nuse(Foo(2));\nuse(bar(3));\n");
    ASSERT_EQ(r.result.facts.scoped_calls.size(), 1u);
    ASSERT_EQ(r.result.casts.size(), 1u);
    ASSERT_EQ(r.result.scoped_casts.size(), 1u);
    const FactRef scoped{FactRef::Kind::ScopedCall, r.result.scoped_casts.begin()->first};
    const FactRef free{FactRef::Kind::FreeCall, r.result.casts.begin()->first};
    ASSERT_NE(r.result.cast_of(scoped), nullptr);
    ASSERT_NE(r.result.cast_of(free), nullptr);
    EXPECT_EQ(r.result.cast_of(scoped)->qualified(), "acme::Foo");
    EXPECT_EQ(r.result.cast_of(free)->name, "Foo");
    EXPECT_EQ(r.result.cast_of(FactRef{FactRef::Kind::FreeCall, 99}), nullptr);
    EXPECT_EQ(r.result.cast_of(FactRef{FactRef::Kind::ScopedCall, 99}), nullptr);
    EXPECT_EQ(r.result.cast_of(FactRef{FactRef::Kind::MethodCall, 0}), nullptr);
    EXPECT_EQ(r.result.cast_of(FactRef{}), nullptr);
}

TEST(ClassifyFree, AMacroLikeNameThatIsAClassStillConstructs) {
    const Outcome r = body("ADC adc(3);\nuse(ADC(4));\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"ADC"});
    EXPECT_EQ(the_class(r, "ADC").constructions.size(), 2u);
    EXPECT_EQ(function_names(r), std::vector<std::string>{"use"});
}

TEST(ClassifyFree, CastsToSomebodyElsesTypesGenerateNothing) {
    const Outcome r = classify(
        "class Fix : public ::testing::Test {};\n"
        "TEST_F(Fix, T) { use(QString(\"x\")); use(size_t(3)); use(Fix()); use(uint8_t(1)); }\n");
    EXPECT_EQ(function_names(r), std::vector<std::string>{"use"});
}

// ---- chains rooted in a cast ----------------------------------------------------------------------------------------------

TEST(ClassifyPending, ACallOnAFunctionalCastOfAKnownClassBecomesAMethodCall) {
    const Outcome r = body("Foo other;\nEXPECT_EQ(Foo(3).size(), 3u);\n");
    ASSERT_EQ(r.result.facts.unresolved.size(), 1u);
    const frontend::Unresolved& u = r.result.facts.unresolved[0];
    ASSERT_TRUE(u.resolved);
    ASSERT_LT(u.method_call, r.result.facts.method_calls.size());

    const frontend::MethodCall& m = r.result.facts.method_calls[u.method_call];
    EXPECT_EQ(m.receiver.name, "Foo");
    EXPECT_EQ(m.method, "size");
    EXPECT_EQ(m.use.kind, UseKind::Compared) << "what the comparison said about the call survives";
    ASSERT_TRUE(m.use.hint.has_value());
    EXPECT_EQ(m.use.hint->spelling, "unsigned int");

    const frontend::ClassTarget& foo = the_class(r, "Foo");
    EXPECT_EQ(foo.method_calls.back(), u.method_call);
    EXPECT_EQ(foo.constructions.size(), 2u) << "`Foo other;` and the cast";
    EXPECT_TRUE(function_names(r).empty());
}

TEST(ClassifyPending, ACallOnAQualifiedCastBecomesAMethodCallToo) {
    const Outcome r = body("acme::Foo other;\nEXPECT_EQ(acme::Foo(3).size(), 3u);\n");
    ASSERT_EQ(r.result.facts.unresolved.size(), 1u);
    const frontend::Unresolved& u = r.result.facts.unresolved[0];
    ASSERT_TRUE(u.resolved);
    ASSERT_LT(u.method_call, r.result.facts.method_calls.size());
    const frontend::MethodCall& m = r.result.facts.method_calls[u.method_call];
    EXPECT_EQ(m.receiver.name, "Foo");
    EXPECT_EQ(m.method, "size");
    EXPECT_EQ(the_class(r, "acme::Foo").method_calls.back(), u.method_call);
    EXPECT_TRUE(function_names(r).empty());
}

TEST(ClassifyPending, AutoVariableOfACastIsTheSame) {
    const Outcome r = body("Foo other;\nauto f = Foo(1);\nf.run();\n");
    ASSERT_EQ(r.result.facts.unresolved.size(), 1u);
    EXPECT_TRUE(r.result.facts.unresolved[0].resolved);
    EXPECT_EQ(the_class(r, "Foo").method_calls.size(), 1u);
}

TEST(ClassifyPending, WithoutEvidenceThatFooIsAClassTheChainStaysUnresolved) {
    const Outcome r = body("EXPECT_EQ(Foo(3).size(), 3u);\n");
    ASSERT_EQ(r.result.facts.unresolved.size(), 1u);
    EXPECT_FALSE(r.result.facts.unresolved[0].resolved);
    EXPECT_EQ(function_names(r), std::vector<std::string>{"Foo"});
}

TEST(ClassifyPending, ACallOnAnExternalCastNeedsNothing) {
    const Outcome r = body("EXPECT_EQ(QString(\"x\").size(), 1);\n");
    // The cast has a type (QString), so the call on it is nobody's business; were
    // there anything left pending it would be marked resolved.
    for (const frontend::Unresolved& u : r.result.facts.unresolved) EXPECT_TRUE(u.resolved);
    EXPECT_TRUE(class_names(r).empty());
    EXPECT_TRUE(function_names(r).empty());
}

TEST(ClassifyPending, AChainRootedInAMethodStaysUnresolved) {
    const Outcome r = body("Foo foo;\nfoo.make().run();\n");
    ASSERT_EQ(r.result.facts.unresolved.size(), 1u);
    EXPECT_FALSE(r.result.facts.unresolved[0].resolved);
    EXPECT_EQ(the_class(r, "Foo").method_calls.size(), 1u);
}

// ---- exception types -------------------------------------------------------------------------------------------------------------

TEST(ClassifyExceptions, AnUnknownExceptionTypeIsReported) {
    const Outcome r = body("Foo foo;\nEXPECT_THROW(foo.parse(\"\"), BadInput);\nEXPECT_THROW(foo.run(), acme::Failure);\n");
    EXPECT_EQ(warnings(r), (std::vector<std::string>{"mt-unknown-type", "mt-unknown-type"}));
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Foo"}) << "an exception class is not invented";
}

TEST(ClassifyExceptions, ExceptionTypesSomebodyProvidesAreFine) {
    frontend::ClassifyOptions options;
    options.known.push_back(header("class FromHeader {};\n"));
    const Outcome r = classify(
        "class Mine {};\n"
        "TEST(S, N) { Foo foo;\n"
        "  EXPECT_THROW(foo.a(), std::runtime_error);\n"
        "  EXPECT_THROW(foo.b(), FromHeader);\n"
        "  EXPECT_THROW(foo.c(), Mine);\n"
        "  EXPECT_THROW(foo.d(), Elsewhere);\n"
        "  Elsewhere e;\n"
        "}\n", options);
    EXPECT_TRUE(warnings(r).empty()) << "Elsewhere is also used as a type, so it will exist";
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"Foo", "Elsewhere"}));
}

// ---- the whole ------------------------------------------------------------------------------------------------------------------------

TEST(ClassifyWhole, ASmallRealisticFile) {
    const Outcome r = classify(
        "#include <gtest/gtest.h>\n"
        "#include \"calc.h\"\n"
        "using namespace acme;\n"
        "class CalcTest : public ::testing::Test {\n"
        "protected:\n"
        "    void SetUp() override { calc_.reset(); }\n"
        "    Calculator calc_;\n"
        "};\n"
        "TEST_F(CalcTest, Adds) {\n"
        "    EXPECT_EQ(calc_.add(2, 3), 5);\n"
        "    EXPECT_EQ(Calculator::version(), 2u);\n"
        "    EXPECT_EQ(clamp_to_byte(300), 255);\n"
        "    Display d(calc_);\n"
        "    d.show();\n"
        "}\n");
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"acme::Calculator", "acme::Display"}));
    EXPECT_EQ(function_names(r), std::vector<std::string>{"acme::clamp_to_byte"});
    EXPECT_EQ(the_class(r, "acme::Calculator").method_calls.size(), 2u);
    EXPECT_EQ(the_class(r, "acme::Calculator").static_calls.size(), 1u);
    EXPECT_EQ(the_class(r, "acme::Calculator").constructions.size(), 1u) << "the fixture's member";
    EXPECT_EQ(the_class(r, "acme::Display").method_calls.size(), 1u);
    EXPECT_TRUE(warnings(r).empty());
}

TEST(ClassifyWhole, ClassifyingTwiceGivesTheSameResultAndDiagnostics) {
    const std::string text =
        "using namespace a;\nusing namespace b;\n"
        "TEST(S, N) { Foo f; Widget::make(1); util::go(); add(1); Bar(2); Foo(3).size(); }\n";
    const Outcome x = classify(text);
    const Outcome y = classify(text);
    EXPECT_EQ(class_names(x), class_names(y));
    EXPECT_EQ(function_names(x), function_names(y));
    EXPECT_EQ(x.diagnostics.sorted(), y.diagnostics.sorted());
}

// ---- test doubles ------------------------------------------------------------------------------------------------

namespace {

std::vector<std::string> interface_of(const Outcome& r, const char* qualified) {
    std::vector<std::string> out;
    for (const ir::Method& m : the_class(r, qualified).interface_methods) {
        std::string text = m.ret.spelling + " " + m.name + "(";
        for (std::size_t i = 0; i < m.params.size(); ++i) text += (i == 0 ? "" : ", ") + m.params[i].type.spelling;
        text += std::string(")") + (m.is_const ? " const" : "");
        out.push_back(text);
    }
    return out;
}

}  // namespace

TEST(ClassifyDoubles, HelpersAndFakesInAnAnonymousNamespaceAreTheFilesOwn) {
    const Outcome r = classify(
        "namespace {\n"
        "int twice(int x) { return x * 2; }\n"
        "struct Params { int a; };\n"
        "class Fake : public Sensor { public: int read() override { return 1; } };\n"
        "}\n"
        "TEST(S, N) { Params p; Fake f; EXPECT_EQ(twice(2), 4); Foo foo(&f); }\n");
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"Sensor", "Foo"}));
    EXPECT_TRUE(function_names(r).empty());
}

TEST(ClassifyDoubles, TheBaseOfAFakeIsAClassToGenerate) {
    const Outcome r = classify(
        "class FakeSensor : public Sensor { public: int read() override { return 5; } };\n"
        "TEST(S, N) { FakeSensor f; }\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Sensor"});
    EXPECT_EQ(r.result.doubles, (std::map<std::string, std::string>{{"FakeSensor", "Sensor"}}));
}

TEST(ClassifyDoubles, TheInterfaceIsWhatTheDoubleOverrides) {
    const Outcome r = classify(
        "class FakeSensor : public Sensor {\n"
        "public:\n"
        "    int read() override { return 5; }\n"
        "    void set(double v, const std::string& unit) override {}\n"
        "    bool ok() const override { return true; }\n"
        "    int closed() final { return 0; }\n"
        "    void helper() {}\n"
        "    int virtual_but_not_override();\n"
        "    static int count();\n"
        "    ~FakeSensor() override {}\n"
        "};\n");
    EXPECT_EQ(interface_of(r, "Sensor"),
              (std::vector<std::string>{"int read()", "void set(double, const std::string&)", "bool ok() const", "int closed()"}));
    for (const ir::Method& m : the_class(r, "Sensor").interface_methods) {
        EXPECT_TRUE(m.is_virtual) << m.name;
        EXPECT_FALSE(m.is_pure) << m.name << ": inference decides that";
        EXPECT_FALSE(m.is_override) << m.name;
        EXPECT_FALSE(m.is_final) << m.name;
        EXPECT_EQ(m.origin, ir::Origin::Declared) << m.name;
    }
}

TEST(ClassifyDoubles, TheMacrosOfAMockSayTheSame) {
    const Outcome r = classify(
        "class MockSensor : public Sensor {\n"
        "public:\n"
        "    MOCK_METHOD(int, read, (), (override));\n"
        "    MOCK_METHOD(void, set, (double, const std::string&), (const, override));\n"
        "    MOCK_METHOD1(write, void(int));\n"
        "    MOCK_CONST_METHOD0(ok, bool());\n"
        "};\n");
    EXPECT_EQ(interface_of(r, "Sensor"),
              (std::vector<std::string>{"int read()", "void set(double, const std::string&) const", "void write(int)", "bool ok() const"}));
}

TEST(ClassifyDoubles, TheClassKnowsItsDoubles) {
    const Outcome r = classify(
        "class FakeSensor : public Sensor {};\n"
        "class Other : public Logger {};\n"
        "class MockSensor : public Sensor {};\n");
    EXPECT_EQ(the_class(r, "Sensor").doubles, (std::vector<std::string>{"FakeSensor", "MockSensor"}));
    EXPECT_EQ(the_class(r, "Logger").doubles, (std::vector<std::string>{"Other"}));
}

TEST(ClassifyDoubles, TwoDoublesOfOneClassAreOneInterface) {
    const Outcome r = classify(
        "class Fake : public Sensor { public: int read() override { return 1; } void a() override {} };\n"
        "class Mock : public Sensor { public: MOCK_METHOD(int, read, (), (override)); MOCK_METHOD(void, b, (), (override)); };\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Sensor"});
    EXPECT_EQ(interface_of(r, "Sensor"), (std::vector<std::string>{"int read()", "void a()", "void b()"}));
    EXPECT_EQ(r.result.doubles.size(), 2u);
}

TEST(ClassifyDoubles, OverloadsAreDifferentFunctions) {
    const Outcome r = classify(
        "class Fake : public Sensor { public: void put(int) override {} void put(double) override {} void put(int) const override {} };\n");
    EXPECT_EQ(interface_of(r, "Sensor"), (std::vector<std::string>{"void put(int)", "void put(double)", "void put(int) const"}));
}

TEST(ClassifyDoubles, TheFrameworkIsNotADoubleBase) {
    const Outcome r = classify(
        "class A : public ::testing::Test {};\n"
        "class B : public testing::TestWithParam<int> {};\n"
        "class C : public testing::WithParamInterface<int> {};\n"
        "TEST_F(A, N) {}\n");
    EXPECT_TRUE(class_names(r).empty());
    EXPECT_TRUE(r.result.doubles.empty());
}

TEST(ClassifyDoubles, AnUnscopedTestWithoutTheUsingDirectiveIsJustAClass) {
    const Outcome r = classify("class A : public Test {};\nTEST_F(A, N) {}\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Test"});
    EXPECT_EQ(r.result.doubles, (std::map<std::string, std::string>{{"A", "Test"}}));
}

TEST(ClassifyDoubles, TheUsingDirectiveDoesNotMakeEveryBaseTheFrameworks) {
    const Outcome r = classify("using namespace testing;\nclass Fake : public Sensor {};\n");
    EXPECT_EQ(r.result.doubles, (std::map<std::string, std::string>{{"Fake", "Sensor"}}));
}

TEST(ClassifyDoubles, ABaseUsedWithoutAQualifierIsFoundInTheNamespaceOfTheTest) {
    const Outcome r = classify("namespace acme {\nclass Fake : public Sensor { public: int read() override { return 1; } };\n}\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"acme::Sensor"});
    EXPECT_EQ(interface_of(r, "acme::Sensor"), std::vector<std::string>{"int read()"});
}

TEST(ClassifyDoubles, ANestedTypeOfAGeneratedClassIsNotAnInterfaceAndNothingBreaks) {
    const Outcome r = classify(
        "class Fake : public Outer::Inner { public: int read() override { return 1; } };\n"
        "TEST(S, N) { Outer o; }\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Outer"});
    EXPECT_TRUE(the_class(r, "Outer").interface_methods.empty());
    EXPECT_EQ(r.result.doubles.at("Fake"), "Outer::Inner");
}

TEST(ClassifyDoubles, WhateverTheAccessOfAnOverrideTheInterfaceIsPublic) {
    const Outcome r = classify(
        "class Fake : public Sensor {\n"
        "private:\n    int a() override { return 1; }\n"
        "protected:\n    int b() override { return 1; }\n"
        "};\n");
    ASSERT_EQ(the_class(r, "Sensor").interface_methods.size(), 2u);
    for (const ir::Method& m : the_class(r, "Sensor").interface_methods) EXPECT_EQ(m.access, ir::Access::Public) << m.name;
}

TEST(ClassifyDoubles, UnscopedFrameworkTypesNeedTheUsingDirective) {
    const Outcome r = classify(
        "using namespace testing;\n"
        "class A : public Test {};\n"
        "class B : public TestWithParam<int> {};\n"
        "TEST_F(A, N) { NiceMock<Foo> m; }\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Foo"});
    EXPECT_TRUE(r.result.doubles.empty());
}

TEST(ClassifyDoubles, AUsingDeclarationBringsOneFrameworkTypeIn) {
    const Outcome r = classify("using testing::Test;\nclass A : public Test {};\nTEST_F(A, N) {}\n");
    EXPECT_TRUE(class_names(r).empty());
    EXPECT_TRUE(r.result.doubles.empty());
}

TEST(ClassifyDoubles, ADoubleOfAClassTheTestIncludesGeneratesNothing) {
    frontend::ClassifyOptions options;
    options.known.push_back(header("class Sensor { public: virtual int read() = 0; };\n"));
    const Outcome r = classify("class Fake : public Sensor { public: int read() override { return 1; } };\n", options);
    EXPECT_TRUE(class_names(r).empty());
    EXPECT_EQ(r.result.doubles, (std::map<std::string, std::string>{{"Fake", "Sensor"}}));
}

TEST(ClassifyDoubles, ADoubleOfSomebodyElsesClassIsStillADouble) {
    const Outcome r = classify(
        "class MyError : public std::runtime_error { public: MyError() : std::runtime_error(\"x\") {} };\n"
        "class FakeWidget : public QWidget {};\n");
    EXPECT_TRUE(class_names(r).empty());
    EXPECT_EQ(r.result.doubles, (std::map<std::string, std::string>{{"FakeWidget", "QWidget"}, {"MyError", "std::runtime_error"}}));
}

TEST(ClassifyDoubles, ADoubleOfADoubleStandsForTheOriginalBase) {
    const Outcome r = classify(
        "class FakeBase : public Sensor { public: int read() override { return 1; } };\n"
        "class Fake : public FakeBase { public: bool ok() const override { return true; } };\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Sensor"});
    EXPECT_EQ(r.result.doubles, (std::map<std::string, std::string>{{"Fake", "Sensor"}, {"FakeBase", "Sensor"}}));
    EXPECT_EQ(interface_of(r, "Sensor"), (std::vector<std::string>{"int read()", "bool ok() const"}));
}

TEST(ClassifyDoubles, AFixtureOverAFakeIsNotADouble) {
    const Outcome r = classify(
        "class FakeSensor : public Sensor { public: int read() override { return 1; } };\n"
        "class F : public ::testing::Test { protected: FakeSensor sensor; };\n"
        "TEST_F(F, N) {}\n");
    EXPECT_EQ(r.result.doubles, (std::map<std::string, std::string>{{"FakeSensor", "Sensor"}}));
}

TEST(ClassifyDoubles, ABaseInANamespaceIsGeneratedThere) {
    const Outcome r = classify(
        "class Fake : public acme::Sensor { public: int read() override { return 1; } };\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"acme::Sensor"});
    EXPECT_EQ(interface_of(r, "acme::Sensor"), std::vector<std::string>{"int read()"});
    EXPECT_EQ(r.result.doubles.at("Fake"), "acme::Sensor");
}

TEST(ClassifyDoubles, SeveralBasesAreOneInterfaceAndANote) {
    const Outcome r = classify(
        "class Fake : public Sensor, public Logger { public: int read() override { return 1; } void log() override {} };\n");
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"Sensor", "Logger"}));
    EXPECT_EQ(interface_of(r, "Sensor"), (std::vector<std::string>{"int read()", "void log()"}));
    EXPECT_TRUE(the_class(r, "Logger").interface_methods.empty());
    EXPECT_EQ(notes(r), std::vector<std::string>{"mt-unsupported"});
}

TEST(ClassifyDoubles, AFrameworkBaseNextToTheRealOneIsNotASecondBase) {
    const Outcome r = classify(
        "class Fake : public Sensor, public testing::Test { public: int read() override { return 1; } };\n");
    EXPECT_TRUE(notes(r).empty());
    EXPECT_EQ(r.result.doubles.at("Fake"), "Sensor");
}

TEST(ClassifyDoubles, ADoubleWithoutOverridesStillStandsForItsBase) {
    const Outcome r = classify("class Fake : public Sensor {};\nTEST(S, N) { Fake f; }\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Sensor"});
    EXPECT_TRUE(the_class(r, "Sensor").interface_methods.empty());
    EXPECT_EQ(r.result.doubles.at("Fake"), "Sensor");
}

TEST(ClassifyDoubles, AClassWithoutBasesIsNotADouble) {
    const Outcome r = classify("struct Params { int a; };\nTEST(S, N) { Params p; }\n");
    EXPECT_TRUE(r.result.doubles.empty());
}

TEST(ClassifyDoubles, ACycleInTheFilesOwnClassesDoesNotHang) {
    const Outcome r = classify("class A : public B {};\nclass B : public A {};\n");
    EXPECT_TRUE(r.result.doubles.empty());
}

TEST(ClassifyDoubles, GmockWrappersAreNotClassesWithUsingNamespaceTesting) {
    const Outcome r = classify(
        "using namespace testing;\n"
        "class MockSensor : public Sensor { public: MOCK_METHOD(int, read, (), (override)); };\n"
        "TEST(S, N) { NiceMock<MockSensor> a; StrictMock<MockSensor> b; NaggyMock<MockSensor> c; Controller k(&a); }\n");
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"Sensor", "Controller"}));
}

TEST(ClassifyWhole, NothingToClassifyIsFine) {
    const Outcome r = classify("");
    EXPECT_TRUE(r.result.classes.empty());
    EXPECT_TRUE(r.result.functions.empty());
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(ClassifyWhole, TheSpikeFileNeedsFooBarAndAFewFunctions) {
    const Outcome r = classify(
        "#include <gtest/gtest.h>\n#include \"foo.h\"\n"
        "class FooFixture : public ::testing::Test { protected: Foo foo_{3}; const Foo& cfoo_ = foo_; };\n"
        "TEST(FooTest, S) {\n"
        "    Foo foo(3, \"abc\"); foo.run(); ns::Bar bar; Foo::create(1.0f); Foo f2();\n"
        "    EXPECT_THROW(foo.parse(\"\"), std::runtime_error);\n"
        "}\n");
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"Foo", "ns::Bar"}));
    EXPECT_TRUE(function_names(r).empty());
}

// ---- enums: Color::Red --------------------------------------------------------------------------------------------------------

namespace {

std::vector<std::string> enum_names(const Outcome& r) {
    std::vector<std::string> out;
    for (const frontend::EnumTarget& e : r.result.enums) out.push_back(e.qualified());
    return out;
}

const frontend::EnumTarget& the_enum(const Outcome& r, const char* qualified) {
    static const frontend::EnumTarget none;
    for (const frontend::EnumTarget& e : r.result.enums) {
        if (e.qualified() == qualified) return e;
    }
    ADD_FAILURE() << "no enum " << qualified;
    return none;
}

}  // namespace

TEST(ClassifyEnums, ANameQualifyingAValueThatNothingElseExplainsIsAnEnumToGenerate) {
    const Outcome r = body("Paint paint;\npaint.set(Color::Red);\nEXPECT_EQ(paint.color(), Color::Blue);\n");
    EXPECT_EQ(enum_names(r), std::vector<std::string>{"Color"});
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Paint"}) << "and not a class";
    EXPECT_EQ(the_enum(r, "Color").enumerators, (std::vector<std::string>{"Red", "Blue"}));
    EXPECT_EQ(the_enum(r, "Color").first_use.line, 5u);
}

TEST(ClassifyEnums, EachEnumeratorIsListedOnceInTheOrderOfItsFirstUseWithWhereItWas) {
    const Outcome r = body("Paint paint;\npaint.set(Color::Green);\npaint.set(Color::Red);\npaint.set(Color::Green);\npaint.set(Color::Blue);\n");
    const frontend::EnumTarget& color = the_enum(r, "Color");
    EXPECT_EQ(color.enumerators, (std::vector<std::string>{"Green", "Red", "Blue"}));
    ASSERT_EQ(color.uses.size(), 3u);
    EXPECT_EQ(color.uses[0].line, 5u);
    EXPECT_EQ(color.uses[1].line, 6u);
    EXPECT_EQ(color.uses[2].line, 8u);
}

TEST(ClassifyEnums, TheValueTakesTheTypeOfTheEnumAndSoDoesWhatItIsComparedWith) {
    const Outcome r = body("Paint paint;\npaint.set(Color::Red);\nEXPECT_EQ(paint.color(), Color::Blue);\nEXPECT_EQ(Color::Green, paint.other());\n");
    const frontend::Classified& c = r.result;
    ASSERT_EQ(c.facts.method_calls.size(), 3u);
    ASSERT_TRUE(c.facts.method_calls[0].args[0].type.has_value());
    EXPECT_EQ(c.facts.method_calls[0].args[0].type->spelling, "Color");
    EXPECT_EQ(c.facts.method_calls[0].args[0].type->origin, ir::Origin::Inferred);
    EXPECT_EQ(c.facts.method_calls[0].args[0].type->reason, "enumerator Color::Red");
    ASSERT_TRUE(c.facts.method_calls[1].use.hint.has_value());
    EXPECT_EQ(c.facts.method_calls[1].use.hint->spelling, "Color");
    ASSERT_TRUE(c.facts.method_calls[2].use.hint.has_value());
    EXPECT_EQ(c.facts.method_calls[2].use.hint->reason, "enumerator Color::Green");
}

TEST(ClassifyEnums, TheNamespaceIsTheOneInTheNameOrTheOneTheTestIsIn) {
    const Outcome qualified = body("Paint paint;\npaint.set(acme::paint::Color::Red);\n");
    EXPECT_EQ(the_enum(qualified, "acme::paint::Color").ns, (std::vector<std::string>{"acme", "paint"}));

    const Outcome nearby = classify("#include <gtest/gtest.h>\nnamespace acme {\nTEST(S, N) { Paint paint; paint.set(Color::Red); }\n}\n");
    EXPECT_EQ(enum_names(nearby), std::vector<std::string>{"acme::Color"});
    EXPECT_EQ(class_names(nearby), std::vector<std::string>{"acme::Paint"});
}

TEST(ClassifyEnums, AnAnnotationNamesTheHeader) {
    const Outcome r = body("// @mt: class Color header \"acme/color.h\"\nPaint paint;\npaint.set(Color::Red);\n");
    EXPECT_EQ(the_enum(r, "Color").header, "acme/color.h");
}

TEST(ClassifyEnums, AVariableOfTheTypeAndACopyFromAnEnumeratorAreWhatEnumsHave) {
    const Outcome r = body("Color c = Color::Red;\nColor d;\nColor e(Color::Blue);\nPaint paint;\npaint.set(c);\n");
    EXPECT_EQ(enum_names(r), std::vector<std::string>{"Color"});
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Paint"}) << "Color is declared as a type, but that does not make it a class";
}

TEST(ClassifyEnums, ComparingWithEnumeratorsDoesNotMakeAClass) {
    const Outcome r = body("Color c = Color::Red;\nPaint paint;\nEXPECT_EQ(c, Color::Red);\nEXPECT_TRUE(c != paint.color());\nEXPECT_EQ(paint.color(), c);\n");
    EXPECT_EQ(enum_names(r), std::vector<std::string>{"Color"});
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Paint"});
}

TEST(ClassifyEnums, WhatTheTestDoesToTheNameWithoutAnEnumeratorMakesItAClass) {
    for (const char* use : {"Limits limits(3);", "Limits limits;\nlimits.value();", "Limits* limits = new Limits;", "Limits::create(1);",
                            "Limits limits;\nint x = limits.field;"}) {
        const Outcome r = body(std::string(use) + "\nint n = Limits::kMax;\n");
        EXPECT_TRUE(r.result.enums.empty()) << use;
        EXPECT_EQ(class_names(r), std::vector<std::string>{"Limits"}) << use;
    }
}

TEST(ClassifyEnums, AnythingNotCapitalisedIsANamespaceNotAnEnum) {
    const Outcome r = body("Paint paint;\npaint.set(acme::kMax);\nint n = paint.limit(config::kDepth);\n");
    EXPECT_TRUE(r.result.enums.empty());
}

TEST(ClassifyEnums, TheLibrariesAndTheFileAreNotEnumsToGenerate) {
    const Outcome r = body("Paint paint;\nEXPECT_EQ(s.find('x'), std::string::npos);\npaint.align(Qt::AlignLeft);\npaint.mode(QString::SkipEmptyParts);\n"
                           "paint.hint(testing::kMax);\npaint.local(Helper::Big);\n",
                           "struct Helper { enum { Big }; };\n");
    EXPECT_TRUE(r.result.enums.empty());
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Paint"});
}

TEST(ClassifyEnums, AnEnumTheFileDeclaresIsNotGeneratedButTypesItsEnumerators) {
    const Outcome r = body("Paint paint;\npaint.set(Color::Red);\n", "enum class Color { Red, Blue };\n");
    EXPECT_TRUE(r.result.enums.empty());
    EXPECT_EQ(r.result.facts.method_calls[0].args[0].type->spelling, "Color");
}

TEST(ClassifyEnums, AnEnumOfAHeaderIsNotGeneratedEither) {
    frontend::ClassifyOptions options;
    options.known.push_back(header("namespace acme { enum class Color { Red, Blue }; }"));
    const Outcome r = body("Paint paint;\npaint.set(acme::Color::Red);\npaint.set(Color::Blue);\n", "", options);
    EXPECT_TRUE(r.result.enums.empty());
    EXPECT_EQ(r.result.facts.method_calls[0].args[0].type->spelling, "acme::Color");
    EXPECT_EQ(r.result.facts.method_calls[1].args[0].type->spelling, "Color");
}

TEST(ClassifyEnums, ATypeNestedInAClassOfTheTestIsNotAnEnumToGenerate) {
    const Outcome r = body("Panel panel;\npanel.set(Panel::Mode::Fast);\n");
    EXPECT_TRUE(r.result.enums.empty()) << "nested types are not generated";
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Panel"});
}

TEST(ClassifyEnums, TwoEnumsAreTwoTargetsInOrderOfFirstUse) {
    const Outcome r = body("Paint paint;\npaint.set(Mode::Fast);\npaint.set(Color::Red);\npaint.set(Mode::Slow);\n");
    EXPECT_EQ(enum_names(r), (std::vector<std::string>{"Mode", "Color"}));
    EXPECT_EQ(the_enum(r, "Mode").enumerators, (std::vector<std::string>{"Fast", "Slow"}));
}

TEST(ClassifyEnums, TheSameNameInTwoNamespacesIsTwoEnums) {
    const Outcome r = body("Paint paint;\npaint.set(a::Color::Red);\npaint.set(b::Color::Red);\n");
    EXPECT_EQ(enum_names(r), (std::vector<std::string>{"a::Color", "b::Color"}));
}

TEST(ClassifyEnums, AGeneratedEnumIsNeverAlsoAClassOfTheSameName) {
    // Something that treats Color like a class would have made one; the enum came first.
    const Outcome r = body("Color c = Color::Red;\nPaint paint;\nEXPECT_EQ(paint.color(), c);\n");
    for (const std::string& name : class_names(r)) EXPECT_NE(name, "Color");
}

// ---- exception types: EXPECT_THROW(stmt, E) -----------------------------------------------------------------------------------

namespace {

frontend::ClassifyOptions throwing(const char* base = "std::runtime_error") {
    frontend::ClassifyOptions options;
    options.config.exception_base = base;
    return options;
}

}  // namespace

TEST(ClassifyExceptions, ByDefaultATypeNobodyProvidesIsOnlyReported) {
    const Outcome r = body("Parser parser;\nEXPECT_THROW(parser.parse(\"x\"), ParseError);\n");
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Parser"});
    EXPECT_EQ(warnings(r), std::vector<std::string>{"mt-unknown-type"});
}

TEST(ClassifyExceptions, WithABaseConfiguredItIsAClassThatIsThrown) {
    const Outcome r = body("Parser parser;\nEXPECT_THROW(parser.parse(\"x\"), ParseError);\n", "", throwing());
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"Parser", "ParseError"}));
    EXPECT_FALSE(the_class(r, "Parser").thrown);
    EXPECT_TRUE(the_class(r, "ParseError").thrown);
    EXPECT_EQ(the_class(r, "ParseError").thrown_at.line, 5u);
    EXPECT_EQ(the_class(r, "ParseError").first_use.line, 5u);
    EXPECT_TRUE(warnings(r).empty());
}

TEST(ClassifyExceptions, TheFirstThrowIsWhereItIsSaidToHaveBeenThrown) {
    const Outcome r = body("Parser parser;\nEXPECT_THROW(parser.parse(\"x\"), ParseError);\nEXPECT_THROW(parser.parse(\"y\"), ParseError);\n", "", throwing());
    EXPECT_EQ(the_class(r, "ParseError").thrown_at.line, 5u);
    EXPECT_EQ(the_class(r, "ParseError").constructions.size(), 0u);
}

TEST(ClassifyExceptions, AClassTheTestAlsoUsesIsThrownAsWell) {
    const Outcome r = body("Parser parser;\nParseError error(3);\nEXPECT_EQ(error.code(), 3);\nEXPECT_THROW(parser.parse(\"x\"), ParseError);\n", "", throwing());
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"Parser", "ParseError"}));
    EXPECT_TRUE(the_class(r, "ParseError").thrown);
    EXPECT_EQ(the_class(r, "ParseError").constructions.size(), 1u);
    EXPECT_EQ(the_class(r, "ParseError").first_use.line, 5u) << "still its first use, not the throw at line 7";
    EXPECT_EQ(the_class(r, "ParseError").thrown_at.line, 7u);
}

TEST(ClassifyExceptions, TheNamespaceIsTheOneInTheNameOrTheTest) {
    const Outcome qualified = body("Parser parser;\nEXPECT_THROW(parser.parse(\"x\"), acme::ParseError);\n", "", throwing());
    EXPECT_EQ(class_names(qualified), (std::vector<std::string>{"Parser", "acme::ParseError"}));
    const Outcome nearby = classify("#include <gtest/gtest.h>\nnamespace acme {\nTEST(S, N) { Parser p; EXPECT_THROW(p.parse(\"x\"), ParseError); }\n}\n", throwing());
    EXPECT_EQ(class_names(nearby), (std::vector<std::string>{"acme::Parser", "acme::ParseError"}));
}

TEST(ClassifyExceptions, WhatSomebodyElseProvidesIsLeftAlone) {
    const Outcome r = body("Parser parser;\nEXPECT_THROW(parser.parse(\"x\"), std::runtime_error);\nEXPECT_THROW(parser.parse(\"y\"), Local);\n"
                           "EXPECT_THROW(parser.parse(\"z\"), Known);\nEXPECT_THROW(parser.parse(\"w\"), QException);\n",
                           "struct Local {};\n", [] {
                               frontend::ClassifyOptions o = throwing();
                               o.known.push_back(header("class Known {};"));
                               return o;
                           }());
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Parser"});
    EXPECT_TRUE(warnings(r).empty());
}

TEST(ClassifyExceptions, WithoutExceptionsThereIsNothingToThrow) {
    frontend::ClassifyOptions options = throwing();
    options.config.no_exceptions = true;
    const Outcome r = body("Parser parser;\nEXPECT_THROW(parser.parse(\"x\"), ParseError);\n", "", options);
    EXPECT_EQ(class_names(r), std::vector<std::string>{"Parser"});
    EXPECT_EQ(warnings(r), std::vector<std::string>{"mt-unknown-type"});
}

TEST(ClassifyExceptions, EveryKindOfThrowAssertionNamesATypeToGenerate) {
    const Outcome r = classify("#include <QtTest>\nclass T : public QObject { Q_OBJECT private slots: void s() {\n"
                               "    Parser p;\n    QVERIFY_EXCEPTION_THROWN(p.parse(\"x\"), ParseError);\n    QVERIFY_THROWS_EXCEPTION(OtherError, p.parse(\"y\"));\n} };\n",
                               throwing());
    EXPECT_EQ(class_names(r), (std::vector<std::string>{"Parser", "ParseError", "OtherError"}));
}

}  // namespace
