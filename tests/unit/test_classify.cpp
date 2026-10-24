// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// "Facts in, targets out": each test reads a fragment of a test file with the
// real frontend, classifies it, and checks which classes and functions the
// tests demand and where they go.

#include <gtest/gtest.h>

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
    ASSERT_EQ(r.result.facts.unresolved.size(), 1u);
    EXPECT_TRUE(r.result.facts.unresolved[0].resolved);
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

}  // namespace
