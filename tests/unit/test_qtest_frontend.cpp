// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Qt Test files, C++ in and facts out, as test_test_frontend.cpp does for
// GoogleTest: the macros that assert, the data that QFETCH declares, the slots
// that are the tests, and the evidence that a file is Qt Test's.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/core/diagnostics.h"
#include "mirrortest/frontend/test_frontend.h"

using namespace mt;
using frontend::UseKind;

namespace {

struct Read {
    frontend::TestFileFacts facts;
    core::Diagnostics       diagnostics;
};

Read read(const std::string& text) {
    Read r;
    r.facts = frontend::read_test_file({"t.cpp", text}, {}, r.diagnostics);
    return r;
}

// `statements` as the body of the slot T::a(); `prelude` goes before the class.
Read slot(const std::string& statements, const std::string& prelude = "") {
    return read("#include <QtTest>\n" + prelude +
                "class T : public QObject {\n    Q_OBJECT\nprivate slots:\n    void a();\n};\n"
                "void T::a() {\n" + statements + "\n}\n");
}

std::vector<std::string> warnings(const Read& r) {
    std::vector<std::string> out;
    for (const core::Diagnostic& d : r.diagnostics.sorted()) {
        if (d.severity == core::Severity::Warning) out.push_back(d.code);
    }
    return out;
}

const frontend::MethodCall& method(const Read& r, std::size_t i = 0) {
    static const frontend::MethodCall none;
    EXPECT_LT(i, r.facts.method_calls.size()) << "method call #" << i;
    return i < r.facts.method_calls.size() ? r.facts.method_calls[i] : none;
}

std::string hint(const frontend::ResultUse& use) { return use.hint ? use.hint->spelling : std::string("<none>"); }

std::vector<std::string> thrown(const Read& r) {
    std::vector<std::string> out;
    for (const frontend::TypeUse& t : r.facts.type_uses) {
        if (t.from_throw_assertion) out.push_back(t.type.qualified());
    }
    return out;
}

bool uses_type(const Read& r, const std::string& name) {
    for (const frontend::TypeUse& t : r.facts.type_uses) {
        if (t.type.qualified() == name) return true;
    }
    return false;
}

// ---- the slots are the tests -----------------------------------------------------------------------

TEST(QtFrontendShape, ASlotDefinedOutsideTheClassIsATestCase) {
    const Read r = slot("");
    ASSERT_EQ(r.facts.tests.size(), 1u);
    EXPECT_EQ(r.facts.tests[0].kind, "SLOT");
    EXPECT_EQ(r.facts.tests[0].suite, "T");
    EXPECT_EQ(r.facts.tests[0].name, "a");
    EXPECT_EQ(r.facts.tests[0].where.line, 7u);
}

TEST(QtFrontendShape, ASlotDefinedInsideTheClassIsOneToo) {
    const Read r = read("#include <QtTest>\nclass T : public QObject {\n    Q_OBJECT\nprivate slots:\n"
                        "    void first() { Foo foo; foo.run(); }\n    void second() {}\n};\n");
    ASSERT_EQ(r.facts.tests.size(), 2u);
    EXPECT_EQ(r.facts.tests[0].name, "first");
    EXPECT_EQ(r.facts.tests[1].name, "second");
    EXPECT_EQ(method(r).method, "run") << "its body is read";
}

TEST(QtFrontendShape, TheFunctionsQtTestCallsAroundTheTestsAreSlotsToo) {
    const Read r = read("#include <QtTest>\nclass T : public QObject {\n    Q_OBJECT\nprivate slots:\n"
                        "    void initTestCase() {}\n    void cleanup() {}\n    void a_data() {}\n    void a() {}\n};\n");
    ASSERT_EQ(r.facts.tests.size(), 4u);
    EXPECT_EQ(r.facts.tests[2].name, "a_data");
}

TEST(QtFrontendShape, OnlyASlotWhoseBodyIsHereCounts) {
    const Read declared = read("#include <QtTest>\nclass T : public QObject {\n    Q_OBJECT\nprivate slots:\n    void a();\n};\n");
    EXPECT_TRUE(declared.facts.tests.empty());
    const Read helper = read("#include <QtTest>\nclass T : public QObject {\n    Q_OBJECT\n    void helper() {}\nprivate slots:\n    void a() {}\n};\n");
    ASSERT_EQ(helper.facts.tests.size(), 1u);
    EXPECT_EQ(helper.facts.tests[0].name, "a") << "a private method that is not a slot is a helper";
}

TEST(QtFrontendShape, TheNamespaceOfTheClassIsKept) {
    const Read r = read("#include <QtTest>\nnamespace acme {\nclass T : public QObject {\n    Q_OBJECT\nprivate slots:\n    void a() {}\n};\n}\n");
    ASSERT_EQ(r.facts.tests.size(), 1u);
    EXPECT_EQ(r.facts.tests[0].ns, std::vector<std::string>{"acme"});
}

// ---- Qt Test is recognised ----------------------------------------------------------------------

TEST(QtFrontendFramework, TheMainMacrosAreEvidence) {
    for (const char* main : {"QTEST_MAIN(T)", "QTEST_APPLESS_MAIN(T)", "QTEST_GUILESS_MAIN(T)", "QTEST_DISABLED_MAIN(T)"}) {
        const Read r = read(std::string("class T {};\n") + main + "\n");
        EXPECT_TRUE(r.facts.qtest_main) << main;
        EXPECT_TRUE(frontend::detect_framework(r.facts).qtest) << main;
    }
    EXPECT_FALSE(read("class T {};\nQTEST_FOO(T)\n").facts.qtest_main);
    EXPECT_FALSE(read("int main() { QTEST_MAIN(T); }\n").facts.qtest_main) << "only at the top of the file";
}

TEST(QtFrontendFramework, ASlotIsEvidenceEvenWithoutAnInclude) {
    const Read r = read("class T : public QObject {\n    Q_OBJECT\nprivate slots:\n    void a() {}\n};\n");
    const frontend::FrameworkEvidence e = frontend::detect_framework(r.facts);
    EXPECT_TRUE(e.qtest);
    EXPECT_FALSE(e.gtest);
}

TEST(QtFrontendFramework, AGoogleTestFileIsNotQtTest) {
    const Read r = read("#include <gtest/gtest.h>\nTEST(S, N) { Foo foo; foo.run(); }\n");
    EXPECT_FALSE(frontend::detect_framework(r.facts).qtest);
    EXPECT_FALSE(r.facts.qtest_main);
}

TEST(QtFrontendFramework, TheMocIncludeIsAnIncludeLikeAnyOther) {
    const Read r = read("#include <QtTest>\n#include \"tst_foo.moc\"\n");
    ASSERT_EQ(r.facts.includes.size(), 2u);
    EXPECT_EQ(r.facts.includes[1].path, "tst_foo.moc");
}

// ---- assertions -----------------------------------------------------------------------------------------

TEST(QtFrontendAssert, QCompareTakesTheTypeOfTheOtherSide) {
    const Read r = slot("Foo foo;\nQCOMPARE(foo.size(), 3u);\nQCOMPARE(2.5f, foo.ratio());\nstd::size_t n = 0;\nQCOMPARE(foo.count(), n);");
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Compared);
    EXPECT_EQ(hint(method(r, 0).use), "unsigned int");
    EXPECT_EQ(hint(method(r, 1).use), "float");
    EXPECT_EQ(hint(method(r, 2).use), "std::size_t");
}

TEST(QtFrontendAssert, AStringLiteralIsAQStringToQtTest) {
    const Read r = slot("Foo foo;\nQCOMPARE(foo.name(), \"abc\");\nQCOMPARE(\"abc\", foo.other());\nQTRY_COMPARE(foo.third(), \"x\");");
    EXPECT_EQ(hint(method(r, 0).use), "QString");
    EXPECT_EQ(hint(method(r, 1).use), "QString");
    EXPECT_EQ(hint(method(r, 2).use), "QString");
}

TEST(QtFrontendAssert, AStringLiteralIsStillAStdStringToGoogleTest) {
    const Read r = read("#include <gtest/gtest.h>\nTEST(S, N) { Foo foo; EXPECT_EQ(foo.name(), \"abc\"); }\n");
    EXPECT_EQ(hint(method(r, 0).use), "std::string");
}

TEST(QtFrontendAssert, EveryComparisonMacroIsRecognised) {
    for (const char* name : {"QCOMPARE", "QCOMPARE_EQ", "QCOMPARE_NE", "QCOMPARE_LT", "QCOMPARE_LE", "QCOMPARE_GT", "QCOMPARE_GE",
                             "QTRY_COMPARE"}) {
        const Read r = slot(std::string("Foo foo;\n") + name + "(foo.size(), 3);");
        ASSERT_EQ(r.facts.method_calls.size(), 1u) << name;
        EXPECT_EQ(method(r).use.kind, UseKind::Compared) << name;
        EXPECT_EQ(hint(method(r).use), "int") << name;
    }
}

TEST(QtFrontendAssert, TheTimeoutOfATryCompareIsCodeToo) {
    const Read r = slot("Foo foo;\nQTRY_COMPARE_WITH_TIMEOUT(foo.size(), 3, foo.timeout());");
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(method(r, 0).method, "size");
    EXPECT_EQ(method(r, 1).method, "timeout");
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Unknown);
}

TEST(QtFrontendAssert, QVerifyGivesBool) {
    const Read r = slot("Foo foo;\nQVERIFY(foo.empty());\nQTRY_VERIFY(foo.full());");
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Condition);
    EXPECT_EQ(hint(method(r, 0).use), "bool");
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Condition);
    EXPECT_EQ(hint(method(r, 1).use), "bool");
}

TEST(QtFrontendAssert, TheMessageOrTimeoutOfAVerifyIsCodeToo) {
    const Read r = slot("Foo foo;\nQVERIFY2(foo.ok(), foo.why());\nQTRY_VERIFY_WITH_TIMEOUT(foo.a(), foo.t());\n"
                        "QTRY_VERIFY2(foo.b(), foo.m());\nQTRY_VERIFY2_WITH_TIMEOUT(foo.c(), foo.n(), foo.u());");
    std::vector<std::string> names;
    for (const frontend::MethodCall& m : r.facts.method_calls) names.push_back(m.method);
    EXPECT_EQ(names, (std::vector<std::string>{"ok", "why", "a", "t", "b", "m", "c", "n", "u"}));
    for (std::size_t i : {0u, 2u, 4u, 6u}) {
        EXPECT_EQ(method(r, i).use.kind, UseKind::Condition) << i;
        EXPECT_EQ(hint(method(r, i).use), "bool") << i;
    }
    for (std::size_t i : {1u, 3u, 5u, 7u, 8u}) EXPECT_EQ(method(r, i).use.kind, UseKind::Unknown) << i;
}

TEST(QtFrontendAssert, ExceptionsInTheOrderOfQt5AndQt6) {
    const Read r = slot("Foo foo;\nQVERIFY_EXCEPTION_THROWN(foo.run(), acme::Old);\n"
                        "QVERIFY_THROWS_EXCEPTION(acme::New, foo.fail());\nQVERIFY_THROWS_NO_EXCEPTION(foo.ok());");
    EXPECT_EQ(thrown(r), (std::vector<std::string>{"acme::Old", "acme::New"}));
    ASSERT_EQ(r.facts.method_calls.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) EXPECT_EQ(method(r, i).use.kind, UseKind::Discarded) << i;
    EXPECT_EQ(method(r, 1).method, "fail");
}

TEST(QtFrontendAssert, ABareExceptionNameIsATypeInEitherOrder) {
    const Read r = slot("Foo foo;\nQVERIFY_EXCEPTION_THROWN(foo.run(), MyError);\nQVERIFY_THROWS_EXCEPTION(Other, foo.fail());");
    EXPECT_EQ(thrown(r), (std::vector<std::string>{"MyError", "Other"}));
}

TEST(QtFrontendAssert, ACommaInsideTheStatementIsNotAnotherArgument) {
    const Read r = slot("Foo foo;\nQVERIFY_THROWS_NO_EXCEPTION(foo.pair(1, 2));\nQVERIFY_THROWS_EXCEPTION(E, foo.pair(1, 2));");
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Discarded);
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Discarded);
}

TEST(QtFrontendAssert, ABraceInitialiserSplitByTheMacroIsStillAllRead) {
    const Read r = slot("Foo foo;\nQVERIFY_THROWS_NO_EXCEPTION(foo.take(std::pair<int, int>{1, 2}));");
    EXPECT_EQ(method(r).method, "take");
}

TEST(QtFrontendAssert, MessagesAndExpectationsSayNothingAboutTheCode) {
    const Read r = slot("QFAIL(\"no\");\nQSKIP(\"later\");\nQWARN(\"careful\");\nQEXPECT_FAIL(\"\", \"known\", Continue);");
    EXPECT_TRUE(r.facts.method_calls.empty());
    EXPECT_TRUE(r.facts.free_calls.empty());
    EXPECT_FALSE(uses_type(r, "Continue"));
    EXPECT_TRUE(warnings(r).empty());
}

TEST(QtFrontendAssert, QTestAgainstAColumnHasNoHintForTheActualValue) {
    const Read r = slot("Foo foo;\nQTEST(foo.value(), \"expected\");");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).use.kind, UseKind::Unknown);
    EXPECT_FALSE(method(r).use.hint.has_value());
}

TEST(QtFrontendAssert, AnythingQLookingThatIsNotAMacroIsAnOrdinaryCall) {
    const Read r = slot("QString s = QStringLiteral(\"x\");\nQDir d(path);\nFoo foo;\nfoo.run();");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).use.kind, UseKind::Discarded);
}

// ---- QFETCH ------------------------------------------------------------------------------------------------

TEST(QtFrontendFetch, DeclaresTheVariableWhateverTheTypeLooksLike) {
    const Read r = slot("QFETCH(int, a);\nQFETCH(unsigned long long, b);\nQFETCH(QString, c);\n"
                        "QFETCH(std::vector<int>, d);\nQFETCH(std::map<int, QString>, e);\n"
                        "Foo foo;\nfoo.use(a, b, c, d, e);");
    EXPECT_TRUE(warnings(r).empty()) << "QFETCH(int, a) is no C++ to a parser, and not a problem";
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    const auto& args = method(r).args;
    ASSERT_EQ(args.size(), 5u);
    ASSERT_TRUE(args[0].type.has_value());
    EXPECT_EQ(args[0].type->spelling, "int");
    EXPECT_EQ(args[1].type->spelling, "unsigned long long");
    EXPECT_EQ(args[2].type->spelling, "QString");
    EXPECT_EQ(args[3].type->spelling, "std::vector<int>");
    EXPECT_EQ(args[4].type->spelling, "std::map<int, QString>");
}

TEST(QtFrontendFetch, TheGlobalVariantIsTheSame) {
    const Read r = slot("QFETCH_GLOBAL(Foo, foo);\nfoo.run();");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).receiver.name, "Foo");
}

TEST(QtFrontendFetch, ACallOnAFetchedObjectHasItsClassAsReceiver) {
    const Read r = slot("QFETCH(acme::Foo, foo);\nfoo.run();");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).receiver.qualified(), "acme::Foo");
    EXPECT_FALSE(method(r).on_const);
}

TEST(QtFrontendFetch, ATypeFetchedIsATypeUsedButNotConstructed) {
    const Read r = slot("QFETCH(acme::Foo, foo);\nQFETCH(std::vector<acme::Bar>, bars);");
    EXPECT_TRUE(uses_type(r, "acme::Foo"));
    EXPECT_TRUE(uses_type(r, "acme::Bar"));
    EXPECT_TRUE(r.facts.constructions.empty()) << "the macro copies a row; it asks nothing of a default constructor";
}

TEST(QtFrontendFetch, AFetchedValueIsAnArgumentWithItsType) {
    const Read r = slot("QFETCH(int, start);\nFoo foo(start);");
    ASSERT_EQ(r.facts.constructions.size(), 1u);
    ASSERT_EQ(r.facts.constructions[0].args.size(), 1u);
    EXPECT_EQ(r.facts.constructions[0].args[0].var_name, "start");
    ASSERT_TRUE(r.facts.constructions[0].args[0].type.has_value());
    EXPECT_EQ(r.facts.constructions[0].args[0].type->spelling, "int");
}

TEST(QtFrontendFetch, ItIsAStatementLikeAnyOtherAndItsScopeEnds) {
    const Read r = slot("{\n    QFETCH(Foo, foo);\n    foo.run();\n}\nfoo.after();");
    ASSERT_EQ(r.facts.method_calls.size(), 1u) << "outside the block foo is nobody's";
}

TEST(QtFrontendFetch, SomethingThatIsNotAFetchIsLeftAlone) {
    const Read r = slot("QFETCHER(int, a);\nFoo foo;\nfoo.run();");
    EXPECT_EQ(method(r).method, "run");
}

// ---- benchmarks and data ------------------------------------------------------------------------------

TEST(QtFrontendBenchmark, TheBlockIsReadAndIsNotAParseError) {
    const Read r = slot("Foo foo;\nQBENCHMARK {\n    foo.run();\n}\nQBENCHMARK_ONCE {\n    foo.once();\n}");
    EXPECT_TRUE(warnings(r).empty());
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(method(r, 0).method, "run");
    EXPECT_EQ(method(r, 1).method, "once");
}

TEST(QtFrontendData, TheTypesOfAColumnAndOfTheValuesInARowAreUsed) {
    const Read r = read("#include <QtTest>\nclass T : public QObject {\n    Q_OBJECT\nprivate slots:\n    void a_data();\n};\n"
                        "void T::a_data() {\n    QTest::addColumn<acme::Foo>(\"foo\");\n"
                        "    QTest::addColumn<std::vector<acme::Bar>>(\"bars\");\n"
                        "    QTest::newRow(\"one\") << acme::Foo(3) << std::vector<acme::Bar>();\n}\n");
    EXPECT_TRUE(warnings(r).empty());
    EXPECT_TRUE(uses_type(r, "acme::Foo"));
    EXPECT_TRUE(uses_type(r, "acme::Bar"));
    bool built = false;
    for (const frontend::ScopedCall& c : r.facts.scoped_calls) built = built || (c.scope == std::vector<std::string>{"acme"} && c.name == "Foo");
    EXPECT_TRUE(built) << "acme::Foo(3) in the row is a call the classifier will see";
}

TEST(QtFrontendData, TheTypeOfAnyScopedCallIsAUseOfIt) {
    const Read r = slot("Helper::make<acme::Gadget>(3);");
    EXPECT_TRUE(uses_type(r, "acme::Gadget"));
    EXPECT_FALSE(uses_type(r, "int"));
    EXPECT_FALSE(uses_type(slot("std::get<0>(t);"), "0"));
}

TEST(QtFrontendData, ADataFunctionOfTheFileDoesNotNeedItsSlot) {
    const Read r = read("#include <QtTest>\nclass T : public QObject {\n    Q_OBJECT\nprivate slots:\n    void a_data() {}\n};\n");
    ASSERT_EQ(r.facts.tests.size(), 1u);
    EXPECT_EQ(r.facts.tests[0].name, "a_data");
}

}  // namespace
