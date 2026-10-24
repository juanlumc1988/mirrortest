// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// "C++ in, facts out": each test feeds the recogniser a fragment of a test
// file and checks what it recorded, never what anyone later decides to do with
// it. Fragments are not compiled -- the classes they use do not exist.

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "mirrortest/core/diagnostics.h"
#include "mirrortest/frontend/test_frontend.h"
#include "test_support.h"

using namespace mt;
using frontend::Construction;
using frontend::FactRef;
using frontend::UseKind;

namespace {

struct Read {
    frontend::TestFileFacts facts;
    core::Diagnostics       diagnostics;
};

Read read(const std::string& text, const frontend::TestFrontendOptions& options = {}) {
    Read r;
    r.facts = frontend::read_test_file({"t.cpp", text}, options, r.diagnostics);
    return r;
}

// `statements` as the body of a TEST; `prelude` goes before it.
Read body(const std::string& statements, const std::string& prelude = "") {
    return read("#include <gtest/gtest.h>\n" + prelude + "\nTEST(S, N) {\n" + statements + "\n}\n");
}

std::vector<std::string> codes(const Read& r, core::Severity severity) {
    std::vector<std::string> out;
    for (const core::Diagnostic& d : r.diagnostics.sorted()) {
        if (d.severity == severity) out.push_back(d.code);
    }
    return out;
}

std::vector<std::string> warnings(const Read& r) { return codes(r, core::Severity::Warning); }
std::vector<std::string> notes(const Read& r) { return codes(r, core::Severity::Note); }

const frontend::MethodCall& method(const Read& r, std::size_t i = 0) {
    static const frontend::MethodCall none;
    EXPECT_LT(i, r.facts.method_calls.size()) << "method call #" << i;
    return i < r.facts.method_calls.size() ? r.facts.method_calls[i] : none;
}

const frontend::Construction& construction(const Read& r, std::size_t i = 0) {
    static const frontend::Construction none;
    EXPECT_LT(i, r.facts.constructions.size()) << "construction #" << i;
    return i < r.facts.constructions.size() ? r.facts.constructions[i] : none;
}

std::string hint(const frontend::ResultUse& use) {
    return use.hint ? use.hint->spelling : std::string("<none>");
}

std::vector<std::string> methods_called(const Read& r) {
    std::vector<std::string> out;
    for (const frontend::MethodCall& m : r.facts.method_calls) out.push_back(m.receiver.qualified() + "::" + m.method);
    return out;
}

// Safe indexing: a missing argument is a failed expectation, not a crash.
const frontend::ValueInfo& arg(const std::vector<frontend::ValueInfo>& args, std::size_t i) {
    static const frontend::ValueInfo none;
    EXPECT_LT(i, args.size()) << "argument #" << i;
    return i < args.size() ? args[i] : none;
}

std::string type_of(const frontend::ValueInfo& v) { return v.type ? v.type->spelling : std::string("<none>"); }

std::vector<std::string> arg_types(const std::vector<frontend::ValueInfo>& args) {
    std::vector<std::string> out;
    for (const frontend::ValueInfo& a : args) out.push_back(a.type ? a.type->spelling : "?");
    return out;
}

// ---- the shape of a test file ---------------------------------------------------------------

TEST(TestFrontendShape, EmptyFileYieldsEmptyFacts) {
    const Read r = read("");
    EXPECT_TRUE(r.facts.tests.empty());
    EXPECT_TRUE(r.facts.method_calls.empty());
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(TestFrontendShape, RecordsTestsWithTheirKindSuiteAndNamespace) {
    const Read r = read(
        "#include <gtest/gtest.h>\n"
        "class Fix : public ::testing::Test {};\n"
        "namespace acme { namespace v2 {\n"
        "TEST(Suite, First) {}\n"
        "TEST_F(Fix, Second) {}\n"
        "} }\n");
    ASSERT_EQ(r.facts.tests.size(), 2u);
    EXPECT_EQ(r.facts.tests[0].kind, "TEST");
    EXPECT_EQ(r.facts.tests[0].suite, "Suite");
    EXPECT_EQ(r.facts.tests[0].name, "First");
    EXPECT_EQ(r.facts.tests[0].ns, (std::vector<std::string>{"acme", "v2"}));
    EXPECT_EQ(r.facts.tests[1].kind, "TEST_F");
    EXPECT_EQ(r.facts.tests[1].suite, "Fix");
    EXPECT_EQ(r.facts.tests[0].where.line, 4u);
}

TEST(TestFrontendShape, CompactNamespaceDefinitionIsTwoLevels) {
    const Read r = read("namespace a::b { TEST(S, N) { Foo f; } }\n");
    ASSERT_EQ(r.facts.tests.size(), 1u);
    EXPECT_EQ(r.facts.tests[0].ns, (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(construction(r).ns, (std::vector<std::string>{"a", "b"}));
}

TEST(TestFrontendShape, IncludesAndUsingNamespaceAreRecorded) {
    const Read r = read(
        "#include <gtest/gtest.h>\n"
        "#include \"acme/foo.h\"\n"
        "using namespace acme;\n"
        "namespace x { using namespace acme::detail; }\n");
    ASSERT_EQ(r.facts.includes.size(), 2u);
    EXPECT_EQ(r.facts.includes[1].path, "acme/foo.h");
    EXPECT_EQ(r.facts.using_namespaces, (std::vector<std::string>{"acme", "acme::detail"}));
}

TEST(TestFrontendShape, TestsUnderConditionalCompilationAreRead) {
    const Read r = read("#ifdef X\nTEST(S, A) { Foo f; }\n#else\nTEST(S, B) { Bar b; }\n#endif\n");
    EXPECT_EQ(r.facts.tests.size(), 2u);
    EXPECT_EQ(r.facts.constructions.size(), 2u);
}

TEST(TestFrontendShape, FrameworkEvidenceComesFromIncludesAndMacros) {
    EXPECT_TRUE(read("#include <gtest/gtest.h>\n").facts.includes.size() == 1u);
    EXPECT_TRUE(frontend::detect_framework(read("#include <gtest/gtest.h>\n").facts).gtest);
    EXPECT_TRUE(frontend::detect_framework(read("#include <gmock/gmock.h>\n").facts).gtest);
    EXPECT_TRUE(frontend::detect_framework(read("TEST(S, N) {}\n").facts).gtest);
    EXPECT_TRUE(frontend::detect_framework(read("#include <QtTest>\n").facts).qtest);
    EXPECT_TRUE(frontend::detect_framework(read("#include <QtTest/QtTest>\n").facts).qtest);
    EXPECT_TRUE(frontend::detect_framework(read("#include <QTest>\n").facts).qtest);
    const frontend::FrameworkEvidence none = frontend::detect_framework(read("#include <vector>\n").facts);
    EXPECT_FALSE(none.gtest);
    EXPECT_FALSE(none.qtest);
}

TEST(TestFrontendShape, ParseErrorsAreReportedOnceAndTheRestIsStillRead) {
    const Read r = read("TEST(S, N) { Foo f; int = ; }\nTEST(S, M) { Bar b; }\n");
    EXPECT_EQ(r.facts.tests.size(), 2u);
    EXPECT_GE(r.facts.constructions.size(), 1u);
    const std::vector<std::string> w = warnings(r);
    EXPECT_FALSE(w.empty());
    for (const std::string& code : w) EXPECT_EQ(code, "mt-parse");
}

// ---- constructions -------------------------------------------------------------------------------

TEST(TestFrontendConstruction, DefaultDirectAndBraceForms) {
    const Read r = body("Foo a;\nFoo b(3, \"x\");\nFoo c{3, 2.5};\n");
    ASSERT_EQ(r.facts.constructions.size(), 3u);

    EXPECT_EQ(construction(r, 0).type.name, "Foo");
    EXPECT_EQ(construction(r, 0).form, Construction::Form::Default);
    EXPECT_TRUE(construction(r, 0).args.empty());

    EXPECT_EQ(construction(r, 1).form, Construction::Form::Direct);
    ASSERT_EQ(construction(r, 1).args.size(), 2u);
    EXPECT_EQ(arg(construction(r, 1).args, 0).text, "3");
    EXPECT_EQ(type_of(arg(construction(r, 1).args, 0)), "int");
    EXPECT_TRUE(arg(construction(r, 1).args, 1).is_string_literal);
    EXPECT_FALSE(arg(construction(r, 1).args, 1).type.has_value());   // the context decides

    EXPECT_EQ(construction(r, 2).form, Construction::Form::Brace);
    EXPECT_EQ(arg_types(construction(r, 2).args), (std::vector<std::string>{"int", "double"}));
}

TEST(TestFrontendConstruction, QualifiedAndTemplatedTypes) {
    const Read r = body("ns::Foo a(1);\nBox<int> b(2);\n::top::Bar c;\n");
    ASSERT_EQ(r.facts.constructions.size(), 3u);
    EXPECT_EQ(construction(r, 0).type.qualified(), "ns::Foo");
    EXPECT_EQ(construction(r, 1).type.name, "Box");
    EXPECT_EQ(construction(r, 1).type.template_args, "int");
    EXPECT_EQ(construction(r, 2).type.qualified(), "top::Bar");
}

TEST(TestFrontendConstruction, CopyInitialisationFromALiteralIsAConvertingConstructor) {
    const Read r = body("Foo f = 5;\nFoo g = \"abc\";\n");
    ASSERT_EQ(r.facts.constructions.size(), 2u);
    EXPECT_EQ(construction(r, 0).form, Construction::Form::Copy);
    ASSERT_EQ(construction(r, 0).args.size(), 1u);
    EXPECT_EQ(arg(construction(r, 0).args, 0).text, "5");
    EXPECT_EQ(construction(r, 1).form, Construction::Form::Copy);
    EXPECT_TRUE(arg(construction(r, 1).args, 0).is_string_literal);
}

TEST(TestFrontendConstruction, FunctionalFormInTheInitialiserIsOneConstruction) {
    const Read r = body("Foo g = Foo(3, 4);\n");
    ASSERT_EQ(r.facts.constructions.size(), 1u);
    EXPECT_EQ(construction(r).form, Construction::Form::Functional);
    EXPECT_EQ(construction(r).args.size(), 2u);
    EXPECT_TRUE(r.facts.free_calls.empty()) << "Foo(3, 4) is not a call to a function named Foo";
}

TEST(TestFrontendConstruction, NewAndMakeUnique) {
    const Read r = body(
        "Foo* p = new Foo(1);\n"
        "auto q = std::make_unique<Bar>(2, 3);\n"
        "auto s = std::make_shared<ns::Baz>();\n");
    ASSERT_EQ(r.facts.constructions.size(), 3u);
    EXPECT_EQ(construction(r, 0).form, Construction::Form::New);
    EXPECT_EQ(construction(r, 0).type.name, "Foo");
    EXPECT_EQ(construction(r, 1).form, Construction::Form::MakeUnique);
    EXPECT_EQ(construction(r, 1).type.name, "Bar");
    EXPECT_EQ(construction(r, 1).args.size(), 2u);
    EXPECT_EQ(construction(r, 2).form, Construction::Form::MakeUnique);
    EXPECT_EQ(construction(r, 2).type.qualified(), "ns::Baz");
    EXPECT_TRUE(r.facts.scoped_calls.empty()) << "make_unique is not a call to the code under test";
}

TEST(TestFrontendConstruction, PointersReferencesAndFundamentalsConstructNothing) {
    const Read r = body("Foo* p = nullptr;\nFoo& ref = other;\nint n = 3;\nstd::size_t m = 0;\nunsigned long long k = 1;\n");
    for (const Construction& c : r.facts.constructions) {
        EXPECT_NE(c.type.name, "Foo") << "pointers and references do not construct";
        EXPECT_NE(c.type.name, "int");
        EXPECT_NE(c.type.name, "long");
    }
}

TEST(TestFrontendConstruction, ArgumentsKnowTheirVariables) {
    const Read r = body("int n = 3;\nconst std::string s = \"a\";\nFoo f(n, s, &n, std::move(s));\n");
    const Construction& c = construction(r, [&] {
        for (std::size_t i = 0; i < r.facts.constructions.size(); ++i) {
            if (r.facts.constructions[i].type.name == "Foo") return i;
        }
        return std::size_t(0);
    }());
    ASSERT_EQ(c.args.size(), 4u);
    EXPECT_EQ(arg(c.args, 0).var_name, "n");
    EXPECT_TRUE(arg(c.args, 0).is_lvalue);
    EXPECT_EQ(type_of(arg(c.args, 0)), "int");
    EXPECT_EQ(type_of(arg(c.args, 1)), "const std::string");
    EXPECT_TRUE(arg(c.args, 1).type.has_value() && arg(c.args, 1).type->is_const);
    EXPECT_TRUE(arg(c.args, 2).is_address_of);
    EXPECT_EQ(arg(c.args, 2).var_name, "n");
    EXPECT_EQ(type_of(arg(c.args, 2)), "int*") << "&n is an int*, not an int";
    EXPECT_TRUE(arg(c.args, 3).is_moved);
}

// tree-sitter reads `Foo f(x);` as the declaration of a function f with an unnamed
// parameter of type x. Inside a test that is never what is meant.
TEST(TestFrontendConstruction, DirectInitialisationFromVariablesIsNotAFunctionDeclaration) {
    const Read r = body("int n = 3;\nFoo f(n, 4);\nf.run();\nBar b(n);\nb.go();\nBaz z(n, n);\nz.stop();\n");
    std::vector<std::string> made;
    for (const Construction& c : r.facts.constructions) made.push_back(c.type.name + "/" + std::to_string(c.args.size()));
    EXPECT_EQ(made, (std::vector<std::string>{"Foo/2", "Bar/1", "Baz/2"}));
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::run", "Bar::go", "Baz::stop"}));
    EXPECT_TRUE(warnings(r).empty());
    EXPECT_EQ(arg(construction(r, 1).args, 0).var_name, "n");
    EXPECT_EQ(type_of(arg(construction(r, 1).args, 0)), "int");
    EXPECT_EQ(construction(r, 2).form, Construction::Form::Direct);
}

TEST(TestFrontendConstruction, ACallInsideDirectInitialisationIsStillACall) {
    const Read r = body(
        "std::unique_ptr<Foo> p;\n"
        "Bar a(move(p));\n"
        "Bar b(make());\n"
        "Bar c(std::move(p));\n"
        "Bar d(ns::build(1), other);\n"
        "Bar e(Foo());\n");
    std::vector<std::string> made;
    for (const Construction& c : r.facts.constructions) made.push_back(c.type.name + "/" + std::to_string(c.args.size()));
    EXPECT_EQ(made, (std::vector<std::string>{"unique_ptr/0", "Bar/1", "Bar/1", "Bar/1", "Bar/2", "Bar/1"}));

    std::vector<std::string> frees;
    for (const frontend::FreeCall& c : r.facts.free_calls) frees.push_back(c.name);
    EXPECT_EQ(frees, (std::vector<std::string>{"move", "make", "Foo"}));
    ASSERT_EQ(r.facts.scoped_calls.size(), 1u);
    EXPECT_EQ(r.facts.scoped_calls[0].name, "build");
    EXPECT_EQ(arg_types(r.facts.scoped_calls[0].args), std::vector<std::string>{"int"});
    EXPECT_TRUE(arg(construction(r, 3).args, 0).is_moved) << "std::move(p) is a move, not a call to a function named move";
    EXPECT_EQ(r.facts.free_calls[0].use.kind, UseKind::Argument);
}

TEST(TestFrontendConstruction, WhatCanOnlyBeADeclarationStaysOne) {
    const Read r = body("Foo a(int);\nFoo b(const Bar&);\nFoo c(Bar x);\nFoo d(Bar*);\nFoo e(unsigned n, double);\nFoo g(Bar x = 1);\n");
    EXPECT_TRUE(r.facts.constructions.empty());
    EXPECT_TRUE(warnings(r).empty());
}

TEST(TestFrontendConstruction, DirectInitialisationOfANonClassFromAVariable) {
    const Read r = body("Foo foo;\nint n(foo.count());\nlong m(n);\nm = 2;\nEXPECT_EQ(foo.size(), m);\n");
    EXPECT_EQ(hint(method(r, 1).use), "long") << "m was declared long even though tree-sitter first read a function";
}

TEST(TestFrontendConstruction, MostVexingParseIsReportedAndNothingIsConstructed) {
    const Read r = body("Foo f();\nFoo g;\n");
    EXPECT_EQ(warnings(r), (std::vector<std::string>{"mt-vexing-parse"}));
    ASSERT_EQ(r.facts.constructions.size(), 1u);
    EXPECT_EQ(construction(r).type.name, "Foo");
    EXPECT_EQ(r.diagnostics.sorted()[0].where.line, 4u);
}

TEST(TestFrontendConstruction, EmptyParenthesesOfAFundamentalAreNotAWarning) {
    const Read r = body("int f();\n");
    EXPECT_TRUE(warnings(r).empty());
}

// ---- receivers ---------------------------------------------------------------------------------------

TEST(TestFrontendReceiver, DeclaredLocalGivesTheClass) {
    const Read r = body("Foo foo;\nfoo.run(1);\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).receiver.name, "Foo");
    EXPECT_EQ(method(r).method, "run");
    EXPECT_FALSE(method(r).on_const);
    EXPECT_FALSE(method(r).via_arrow);
    EXPECT_EQ(arg_types(method(r).args), std::vector<std::string>{"int"});
}

TEST(TestFrontendReceiver, ConstObjectsAndConstReferencesAreConst) {
    const Read r = body("const Foo a;\nconst Foo& b = a;\nFoo c;\nFoo& d = c;\na.x();\nb.x();\nc.x();\nd.x();\n");
    ASSERT_EQ(r.facts.method_calls.size(), 4u);
    EXPECT_TRUE(method(r, 0).on_const);
    EXPECT_TRUE(method(r, 1).on_const);
    EXPECT_FALSE(method(r, 2).on_const);
    EXPECT_FALSE(method(r, 3).on_const);
}

TEST(TestFrontendReceiver, ArrowOnRawAndConstPointers) {
    const Read r = body("Foo* p = nullptr;\nconst Foo* q = nullptr;\np->a();\nq->b();\n");
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(method(r, 0).receiver.name, "Foo");
    EXPECT_TRUE(method(r, 0).via_arrow);
    EXPECT_FALSE(method(r, 0).on_const);
    EXPECT_TRUE(method(r, 1).on_const);
}

TEST(TestFrontendReceiver, ArrowOnSmartPointers) {
    const Read r = body(
        "std::unique_ptr<Foo> u;\n"
        "std::shared_ptr<const Bar> s;\n"
        "QScopedPointer<Baz> qs;\n"
        "QSharedPointer<Qux> qh;\n"
        "QPointer<Widget> qp;\n"
        "u->a();\ns->b();\nqs->c();\nqh->d();\nqp->e();\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{
        "Foo::a", "Bar::b", "Baz::c", "Qux::d", "Widget::e"}));
    EXPECT_FALSE(method(r, 0).on_const);
    EXPECT_TRUE(method(r, 1).on_const) << "shared_ptr<const Bar> points at a const object";
}

TEST(TestFrontendReceiver, DotOnASmartPointerIsAboutThePointerNotTheClass) {
    const Read r = body("std::unique_ptr<Foo> u;\nu.reset();\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).receiver.name, "unique_ptr");
    EXPECT_EQ(method(r).receiver.scope, std::vector<std::string>{"std"});
}

TEST(TestFrontendReceiver, ExplicitDereferenceIsTheSameAsArrow) {
    const Read r = body(
        "Foo* p = nullptr;\n"
        "std::unique_ptr<Bar> u;\n"
        "(*p).a();\n(*u).b();\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::a", "Bar::b"}));
    EXPECT_FALSE(method(r, 0).via_arrow) << "it was spelled with a dot";
}

TEST(TestFrontendReceiver, ArrowOnAValueOrDotOnAPointerIsNotACall) {
    const Read r = body("Foo foo;\nFoo* p = nullptr;\nfoo->a();\np.b();\n");
    EXPECT_TRUE(r.facts.method_calls.empty());
}

TEST(TestFrontendReceiver, ThisIsTheFixtureAndCallsOnItAreUnqualifiedCalls) {
    const Read r = read(
        "class Fix : public ::testing::Test {\n"
        "protected:\n"
        "    Foo foo_;\n"
        "    void check() { this->helper(1); this->foo_.run(); }\n"
        "};\n");
    ASSERT_EQ(r.facts.free_calls.size(), 1u);
    EXPECT_EQ(r.facts.free_calls[0].name, "helper");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).receiver.name, "Foo");
}

TEST(TestFrontendReceiver, UnknownReceiverIsANoteAndNoCall) {
    const Read r = body("mystery.run();\n");
    EXPECT_TRUE(r.facts.method_calls.empty());
    EXPECT_EQ(notes(r), (std::vector<std::string>{"mt-unknown-receiver"}));
}

TEST(TestFrontendReceiver, AutoVariableHoldingACallResultIsAChainNotANote) {
    const Read r = body("auto x = something();\nx.run();\n");
    EXPECT_TRUE(r.facts.method_calls.empty());
    ASSERT_EQ(r.facts.free_calls.size(), 1u);
    EXPECT_EQ(r.facts.free_calls[0].use.kind, UseKind::AutoBound);
    ASSERT_EQ(r.facts.unresolved.size(), 1u);
    EXPECT_EQ(r.facts.unresolved[0].root.kind, FactRef::Kind::FreeCall);
    EXPECT_TRUE(notes(r).empty());
}

TEST(TestFrontendReceiver, VariableOfAnUndeducibleTypeIsANote) {
    const Read r = body("auto generic = [](auto x) { x.run(); };\n");
    EXPECT_TRUE(r.facts.method_calls.empty());
    EXPECT_EQ(notes(r), (std::vector<std::string>{"mt-unknown-receiver"}));
    EXPECT_TRUE(r.facts.unresolved.empty());
}

TEST(TestFrontendReceiver, AutoFromALiteralOrAConstructionKeepsItsType) {
    const Read r = body("auto n = 3u;\nauto f = Foo(1);\nf.run();\n");
    // Foo(1) is not a declared `Foo f`, so the frontend does not guess: it is a
    // call to a function named Foo that classification will recognise.
    EXPECT_TRUE(r.facts.method_calls.empty());
    EXPECT_EQ(r.facts.free_calls.size(), 1u);
    EXPECT_EQ(r.facts.free_calls[0].name, "Foo");
}

TEST(TestFrontendReceiver, ShadowingFollowsTheInnermostDeclaration) {
    const Read r = body("Foo x;\n{\n    Bar x;\n    x.inner();\n}\nx.outer();\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Bar::inner", "Foo::outer"}));
}

TEST(TestFrontendReceiver, AVariableDoesNotOutliveItsScope) {
    const Read r = body("{\n    Foo x;\n}\nx.run();\n");
    EXPECT_TRUE(r.facts.method_calls.empty());
    EXPECT_EQ(notes(r), (std::vector<std::string>{"mt-unknown-receiver"}));
}

TEST(TestFrontendReceiver, FundamentalsAndTemplatesOfFundamentalsAreNotReceivers) {
    const Read r = body("int n = 0;\nn.foo();\n");
    EXPECT_TRUE(r.facts.method_calls.empty());
}

TEST(TestFrontendReceiver, QualifiedAndTemplatedReceivers) {
    const Read r = body("ns::Foo a;\nBox<int> b;\na.x();\nb.y();\n");
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(method(r, 0).receiver.qualified(), "ns::Foo");
    EXPECT_EQ(method(r, 1).receiver.name, "Box");
    EXPECT_EQ(method(r, 1).receiver.template_args, "int");
}

// ---- chained calls ---------------------------------------------------------------------------------------

TEST(TestFrontendChain, ACallOnAResultIsUnresolvedNotGuessed) {
    const Read r = body("Foo foo;\nfoo.make().run();\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).method, "make");
    EXPECT_EQ(method(r).use.kind, UseKind::Receiver);
    ASSERT_EQ(r.facts.unresolved.size(), 1u);
    EXPECT_EQ(r.facts.unresolved[0].kind, frontend::Unresolved::Kind::ChainedCall);
    EXPECT_EQ(r.facts.unresolved[0].text, "foo.make().run()");
    EXPECT_TRUE(notes(r).empty()) << "a chain is not an 'unknown receiver'";
}

TEST(TestFrontendChain, EveryLinkOfALongerChainHangsFromTheOneBefore) {
    // What a() returns is the unknown. b() is a call on it and c() a call on
    // b()'s result: each link is kept, rooted in the previous one, so that they
    // can be resolved one at a time once the types are known.
    const Read r = body("Foo foo;\nfoo.a().b().c();\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::a"}));
    ASSERT_EQ(r.facts.unresolved.size(), 2u);
    EXPECT_EQ(r.facts.unresolved[0].text, "foo.a().b()");
    EXPECT_EQ(r.facts.unresolved[0].call.method, "b");
    EXPECT_EQ(r.facts.unresolved[0].root.kind, FactRef::Kind::MethodCall);
    EXPECT_EQ(r.facts.unresolved[0].root.index, 0u);
    EXPECT_EQ(r.facts.unresolved[1].text, "foo.a().b().c()");
    EXPECT_EQ(r.facts.unresolved[1].call.method, "c");
    EXPECT_EQ(r.facts.unresolved[1].root.kind, FactRef::Kind::Unresolved);
    EXPECT_EQ(r.facts.unresolved[1].root.index, 0u);
    EXPECT_EQ(r.facts.unresolved[0].call.use.kind, UseKind::Receiver) << "b()'s result is the receiver of c()";
}

TEST(TestFrontendChain, TheRootOfTheChainIsRemembered) {
    // `Foo(3).size()`: a functional cast, or a function named Foo? Syntax cannot
    // say, so the fact keeps the root for classification to settle (7.7).
    const Read r = body("EXPECT_EQ(Foo(3).size(), 3u);\nauto n = Foo::make().count();\n");
    ASSERT_EQ(r.facts.unresolved.size(), 2u);
    EXPECT_EQ(r.facts.unresolved[0].root.kind, FactRef::Kind::FreeCall);
    EXPECT_EQ(r.facts.free_calls[r.facts.unresolved[0].root.index].name, "Foo");
    EXPECT_EQ(r.facts.unresolved[1].root.kind, FactRef::Kind::ScopedCall);
    EXPECT_EQ(r.facts.scoped_calls[r.facts.unresolved[1].root.index].name, "make");
}

TEST(TestFrontendChain, ACallOnAFieldReadIsUnresolvedToo) {
    const Read r = body("Foo foo;\nfoo.child.run();\n");
    ASSERT_EQ(r.facts.field_accesses.size(), 1u);
    EXPECT_EQ(r.facts.field_accesses[0].field, "child");
    EXPECT_EQ(r.facts.unresolved.size(), 1u);
}

// ---- how results are used (design 7.6) ---------------------------------------------------------

TEST(TestFrontendUse, DiscardedResult) {
    const Read r = body("Foo foo;\nfoo.run();\n");
    EXPECT_EQ(method(r).use.kind, UseKind::Discarded);
    EXPECT_FALSE(method(r).use.hint.has_value());
}

TEST(TestFrontendUse, ComparedWithALiteralTakesItsType) {
    const Read r = body("Foo foo;\nEXPECT_EQ(foo.size(), 3u);\nASSERT_NE(foo.ratio(), 2.5f);\nEXPECT_LT(foo.c(), 'x');\n");
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Compared);
    EXPECT_EQ(hint(method(r, 0).use), "unsigned int");
    EXPECT_EQ(hint(method(r, 1).use), "float");
    EXPECT_EQ(hint(method(r, 2).use), "char");
}

TEST(TestFrontendUse, ComparisonMacrosAreAllRecognised) {
    for (const char* name : {"EXPECT_EQ", "EXPECT_NE", "EXPECT_LT", "EXPECT_LE", "EXPECT_GT", "EXPECT_GE",
                             "ASSERT_EQ", "ASSERT_NE", "ASSERT_LT", "ASSERT_LE", "ASSERT_GT", "ASSERT_GE"}) {
        const Read r = body(std::string("Foo foo;\n") + name + "(foo.size(), 3);\n");
        ASSERT_EQ(r.facts.method_calls.size(), 1u) << name;
        EXPECT_EQ(method(r).use.kind, UseKind::Compared) << name;
        EXPECT_EQ(hint(method(r).use), "int") << name;
    }
}

TEST(TestFrontendUse, ComparedWithAStringLiteralIsStdString) {
    const Read r = body("Foo foo;\nEXPECT_EQ(foo.name(), \"abc\");\nEXPECT_EQ(\"abc\", foo.other());\n");
    EXPECT_EQ(hint(method(r, 0).use), "std::string");
    EXPECT_EQ(hint(method(r, 1).use), "std::string");
}

TEST(TestFrontendUse, ComparedWithAVariableTakesItsDeclaredType) {
    const Read r = body("Foo foo;\nstd::size_t n = 0;\nEXPECT_EQ(foo.size(), n);\n");
    EXPECT_EQ(hint(method(r).use), "std::size_t");
}

TEST(TestFrontendUse, TwoCallsComparedGiveNoHintButAreCompared) {
    const Read r = body("Foo a;\nFoo b;\nEXPECT_EQ(a.size(), b.size());\n");
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Compared);
    EXPECT_FALSE(method(r, 0).use.hint.has_value());
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Compared);
}

TEST(TestFrontendUse, ComparedAgainstANegativeLiteralKeepsTheSign) {
    const Read r = body("Foo foo;\nEXPECT_EQ(foo.delta(), -3);\nEXPECT_EQ(foo.big(), -5000000000);\n");
    EXPECT_EQ(hint(method(r, 0).use), "int");
    EXPECT_EQ(hint(method(r, 1).use), "long long");
}

TEST(TestFrontendUse, TruthAssertionsGiveBool) {
    const Read r = body("Foo foo;\nEXPECT_TRUE(foo.empty());\nASSERT_FALSE(foo.full());\n");
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Condition);
    EXPECT_EQ(hint(method(r, 0).use), "bool");
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Condition);
    EXPECT_EQ(hint(method(r, 1).use), "bool");
}

TEST(TestFrontendUse, CStringAssertionsGiveConstCharPointer) {
    const Read r = body("Foo foo;\nEXPECT_STREQ(foo.c_str(), \"abc\");\nASSERT_STRNE(foo.a(), foo.b());\n"
                        "EXPECT_STRCASEEQ(foo.c(), \"X\");\nEXPECT_STRCASENE(foo.d(), \"Y\");\n");
    ASSERT_EQ(r.facts.method_calls.size(), 5u);
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(method(r, i).use.kind, UseKind::Compared) << i;
        EXPECT_EQ(hint(method(r, i).use), "const char*") << i;
    }
}

TEST(TestFrontendUse, WideStringAssertionsGiveConstWcharPointer) {
    const Read r = body("Foo foo;\nwchar_t w = 0;\nEXPECT_STREQ(foo.c_str(), L\"abc\");\n");
    // The literal L"abc" has a type of its own; what the assertion implies is
    // the narrow/wide pointer, decided by the other side.
    EXPECT_EQ(method(r).use.kind, UseKind::Compared);
    EXPECT_EQ(hint(method(r).use), "const wchar_t*");
}

TEST(TestFrontendUse, FloatingPointAssertions) {
    const Read r = body("Foo foo;\nEXPECT_FLOAT_EQ(foo.f(), 1.0f);\nASSERT_DOUBLE_EQ(foo.d(), 1.0);\n"
                        "EXPECT_NEAR(foo.n(), 1.0, 1e-3);\n");
    EXPECT_EQ(hint(method(r, 0).use), "float");
    EXPECT_EQ(hint(method(r, 1).use), "double");
    EXPECT_EQ(hint(method(r, 2).use), "double");
    for (std::size_t i = 0; i < 3; ++i) EXPECT_EQ(method(r, i).use.kind, UseKind::Compared);
}

TEST(TestFrontendUse, NearsToleranceIsEvaluatedButNotCompared) {
    const Read r = body("Foo foo;\nEXPECT_NEAR(1.0, 1.0, foo.tolerance());\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).use.kind, UseKind::Unknown);
}

TEST(TestFrontendUse, EachCallIsClaimedByItsOwnContext) {
    const Read r = body("Foo foo;\nbar(foo.size());\nEXPECT_EQ(foo.size(), 3);\n");
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Argument);
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Compared);
}

TEST(TestFrontendUse, TheContextThatClaimsAResultFirstKeepsIt) {
    // `auto r = foo.make();` claims make() as AutoBound. Calling r.run() later is
    // a use of the same result as a receiver; it must not take the claim away.
    const Read r = body("Foo foo;\nauto r = foo.make();\nr.run();\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).use.kind, UseKind::AutoBound);
}

TEST(TestFrontendUse, ArgumentOfAnotherCall) {
    const Read r = body("Foo foo;\nBar bar(foo.size());\nbaz(foo.count());\nfoo.set(foo.get());\n");
    ASSERT_EQ(r.facts.method_calls.size(), 4u);
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Argument);
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Argument);
    EXPECT_EQ(method(r, 2).use.kind, UseKind::Argument);
    EXPECT_FALSE(method(r, 0).use.hint.has_value()) << "the parameter type of Bar says nothing yet";
}

TEST(TestFrontendUse, AssignedToADeclaredVariable) {
    const Read r = body("Foo foo;\nint n = foo.count();\nconst std::string& s = foo.name();\nunsigned u;\nu = foo.u();\n");
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Assigned);
    EXPECT_EQ(hint(method(r, 0).use), "int");
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Assigned);
    EXPECT_EQ(hint(method(r, 1).use), "const std::string&");
    EXPECT_EQ(method(r, 2).use.kind, UseKind::Assigned);
    EXPECT_EQ(hint(method(r, 2).use), "unsigned");
}

TEST(TestFrontendUse, DirectAndBraceInitialisationOfANonClassBindsTheValue) {
    const Read r = body("Foo foo;\nint a(foo.x());\ndouble d{foo.y()};\nauto e(foo.z());\nunsigned long u{foo.w()};\n");
    ASSERT_EQ(r.facts.method_calls.size(), 4u);
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Assigned);
    EXPECT_EQ(hint(method(r, 0).use), "int");
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Assigned);
    EXPECT_EQ(hint(method(r, 1).use), "double");
    EXPECT_EQ(method(r, 2).use.kind, UseKind::AutoBound);
    EXPECT_EQ(hint(method(r, 3).use), "unsigned long");
}

TEST(TestFrontendUse, PointerInitialisedWithParenthesesAndACallDoesNotLoseTheRest) {
    // tree-sitter reads `const char* s(foo.w());` as a function declarator with
    // an ERROR inside. Whatever the grammar does with it, the following
    // statements must still be read.
    const Read r = body("Foo foo;\nconst char* s(foo.w());\nEXPECT_EQ(foo.size(), 3u);\n");
    bool compared = false;
    for (const frontend::MethodCall& m : r.facts.method_calls) {
        if (m.method == "size") compared = m.use.kind == UseKind::Compared;
    }
    EXPECT_TRUE(compared);
}

TEST(TestFrontendUse, ArgumentsOfAClassConstructionAreArguments) {
    const Read r = body("Foo foo;\nBar bar(foo.w());\nBaz baz{foo.v()};\n");
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Argument);
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Argument);
}

TEST(TestFrontendUse, AutoBoundIsExactlyTheMissingInformation) {
    const Read r = body("Foo foo;\nauto a = foo.f();\nauto& b = foo.g();\nconst auto& c = foo.h();\n");
    ASSERT_EQ(r.facts.method_calls.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(method(r, i).use.kind, UseKind::AutoBound) << i;
        EXPECT_FALSE(method(r, i).use.hint.has_value()) << i;
    }
}

TEST(TestFrontendUse, AutoBoundToALiteralKnowsTheTypeOfTheVariable) {
    const Read r = body("auto n = 3u;\nFoo foo;\nEXPECT_EQ(foo.size(), n);\n");
    EXPECT_EQ(hint(method(r).use), "unsigned int");
}

TEST(TestFrontendUse, ConditionsOfControlFlowAreBool) {
    const Read r = body(
        "Foo foo;\n"
        "if (foo.a()) {}\n"
        "while (foo.b()) {}\n"
        "for (; foo.c(); ) {}\n"
        "do {} while (foo.d());\n"
        "bool t = foo.e() ? true : false;\n");
    ASSERT_EQ(r.facts.method_calls.size(), 5u);
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(method(r, i).use.kind, UseKind::Condition) << i;
        EXPECT_EQ(hint(method(r, i).use), "bool") << i;
    }
}

TEST(TestFrontendUse, LogicalOperatorsAndNegationAreBool) {
    const Read r = body("Foo foo;\nbool b = foo.a() && foo.b() || !foo.c();\n");
    ASSERT_EQ(r.facts.method_calls.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(method(r, i).use.kind, UseKind::Condition) << i;
        EXPECT_EQ(hint(method(r, i).use), "bool") << i;
    }
}

TEST(TestFrontendUse, RelationalOperatorsTakeTheOtherSide) {
    const Read r = body("Foo foo;\nbool b = foo.size() > 3u && 2.5 <= foo.ratio() && foo.n() != 'x';\n");
    // The && pairs the relational results with bool; the comparisons themselves
    // claim their operands first.
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Compared);
    EXPECT_EQ(hint(method(r, 0).use), "unsigned int");
    EXPECT_EQ(hint(method(r, 1).use), "double");
    EXPECT_EQ(hint(method(r, 2).use), "char");
}

TEST(TestFrontendUse, ForRangeAndIfDeclarationsFeedTheTable) {
    const Read r = body(
        "std::vector<Foo> v;\n"
        "for (const Foo& f : v) { f.run(); }\n"
        "if (Bar b = make()) { b.use(); }\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::run", "Bar::use"}));
    EXPECT_TRUE(method(r, 0).on_const);
}

TEST(TestFrontendUse, ReturnedAndThrownValuesAreEvaluatedButUnclaimed) {
    const Read r = read("int helper(Foo& foo) { return foo.size(); }\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).receiver.name, "Foo");
    EXPECT_EQ(method(r).use.kind, UseKind::Unknown);
}

// ---- assertion forms ----------------------------------------------------------------------------------

TEST(TestFrontendAssert, StreamedMessagesAreUnwrapped) {
    const Read r = body("Foo foo;\nEXPECT_EQ(foo.size(), 3u) << \"size was \" << foo.count();\nASSERT_TRUE(foo.ok()) << \"msg\";\n");
    ASSERT_EQ(r.facts.method_calls.size(), 3u);
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Compared);
    EXPECT_EQ(method(r, 1).method, "count");
    EXPECT_EQ(method(r, 2).use.kind, UseKind::Condition);
}

TEST(TestFrontendAssert, ThrowAssertionsAnalyseTheStatementAndRecordTheType) {
    const Read r = body("Foo foo;\nEXPECT_THROW(foo.run(), acme::BadThing);\nASSERT_ANY_THROW(foo.fail());\nEXPECT_NO_THROW(foo.ok());\n");
    ASSERT_EQ(r.facts.method_calls.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) EXPECT_EQ(method(r, i).use.kind, UseKind::Discarded) << i;

    bool found = false;
    for (const frontend::TypeUse& t : r.facts.type_uses) {
        if (t.type.qualified() == "acme::BadThing") {
            found = true;
            EXPECT_TRUE(t.from_throw_assertion);
        }
    }
    EXPECT_TRUE(found);
}

TEST(TestFrontendAssert, ABareExceptionNameIsATypeToo) {
    // In a macro argument the parser calls MyError an identifier, not a type.
    const Read r = body("Foo foo;\nEXPECT_THROW(foo.run(), MyError);\nEXPECT_THROW(foo.run(), ns::Other);\n"
                        "EXPECT_ANY_THROW(foo.run());\n");
    std::vector<std::string> thrown;
    for (const frontend::TypeUse& t : r.facts.type_uses) {
        if (t.from_throw_assertion) thrown.push_back(t.type.qualified());
    }
    EXPECT_EQ(thrown, (std::vector<std::string>{"MyError", "ns::Other"}));
}

TEST(TestFrontendAssert, ABlockAsTheStatementArgument) {
    const Read r = body("Foo foo;\nASSERT_NO_THROW({\n    foo.a();\n    int n = foo.b();\n});\n");
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Discarded);
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Assigned);
}

TEST(TestFrontendAssert, DeathAssertionsAnalyseTheStatement) {
    const Read r = body("Foo foo;\nEXPECT_DEATH(foo.crash(), \"boom\");\nASSERT_EXIT(foo.quit(), ::testing::ExitedWithCode(0), \"\");\n");
    ASSERT_GE(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Discarded);
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Discarded);
}

TEST(TestFrontendAssert, MatchersGiveNoHintButTheValueIsStillSeen) {
    const Read r = body("Foo foo;\nEXPECT_THAT(foo.values(), ::testing::ElementsAre(1, 2));\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).use.kind, UseKind::Unknown);
    EXPECT_FALSE(method(r).use.hint.has_value());
}

TEST(TestFrontendAssert, MockSetupIsNotCodeUnderTest) {
    const Read r = body("MockFoo mock;\nEXPECT_CALL(mock, run(1)).Times(2);\nON_CALL(mock, size()).WillByDefault(Return(3));\n");
    EXPECT_TRUE(r.facts.method_calls.empty());
    EXPECT_TRUE(r.facts.free_calls.empty()) << "EXPECT_CALL and ON_CALL are not functions to generate";
    EXPECT_TRUE(notes(r).empty());
}

TEST(TestFrontendAssert, MessageOnlyMacrosAreNotCallsButWhatIsStreamedIntoThemIs) {
    const Read r = body("Foo foo;\nif (!foo.ok()) FAIL() << \"bad \" << foo.why();\nGTEST_SKIP() << \"later\";\nSUCCEED();\nADD_FAILURE();\n");
    EXPECT_TRUE(r.facts.free_calls.empty());
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::ok", "Foo::why"}));
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Unknown) << "a streamed value implies no type";
}

TEST(TestFrontendAssert, MockSetupChainsAreSkippedWhole) {
    const Read r = body("Foo foo;\nEXPECT_CALL(mock, run(foo.size())).Times(2).WillOnce(Return(foo.value()));\n"
                        "ON_CALL(mock, f()).WillByDefault(testing::Invoke(helper));\n");
    EXPECT_TRUE(r.facts.method_calls.empty());
    EXPECT_TRUE(r.facts.free_calls.empty());
    EXPECT_TRUE(r.facts.scoped_calls.empty());
}

TEST(TestFrontendAssert, AnAssertionNestedInAnotherIsStillNotACall) {
    const Read r = body("Foo foo;\nASSERT_NO_THROW(EXPECT_EQ(foo.a(), 1));\n");
    EXPECT_TRUE(r.facts.free_calls.empty());
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).use.kind, UseKind::Compared);
}

TEST(TestFrontendAssert, UnknownAssertionMacrosEvaluateTheirArguments) {
    const Read r = body("Foo foo;\nEXPECT_PRED1(is_even, foo.value());\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).use.kind, UseKind::Unknown);
}

TEST(TestFrontendAssert, AssertionsAreNeverFreeCalls) {
    const Read r = body("Foo foo;\nEXPECT_EQ(foo.a(), 1);\nEXPECT_TRUE(foo.b());\n");
    EXPECT_TRUE(r.facts.free_calls.empty());
}

TEST(TestFrontendAssert, TypedTestsAreUnsupportedAndTheirBodiesSkipped) {
    const Read r = read(
        "TYPED_TEST_SUITE(Fix, Types);\n"
        "TYPED_TEST(Fix, Works) { Foo foo; foo.run(); }\n");
    EXPECT_EQ(warnings(r), (std::vector<std::string>{"mt-unsupported"}));
    EXPECT_TRUE(r.facts.method_calls.empty());
    EXPECT_TRUE(r.facts.tests.empty());
}

// ---- other calls and field reads ----------------------------------------------------------------------------

TEST(TestFrontendCalls, FreeCallsAreRecordedUnclassified) {
    const Read r = body("int n = compute(3, 4);\nreport();\nEXPECT_EQ(twice(2), 4);\n");
    ASSERT_EQ(r.facts.free_calls.size(), 3u);
    EXPECT_EQ(r.facts.free_calls[0].name, "compute");
    EXPECT_EQ(r.facts.free_calls[0].use.kind, UseKind::Assigned);
    EXPECT_EQ(hint(r.facts.free_calls[0].use), "int");
    EXPECT_EQ(r.facts.free_calls[1].use.kind, UseKind::Discarded);
    EXPECT_EQ(r.facts.free_calls[2].use.kind, UseKind::Compared);
    EXPECT_EQ(arg_types(r.facts.free_calls[0].args), (std::vector<std::string>{"int", "int"}));
}

TEST(TestFrontendCalls, ScopedCallsKeepTheirScopeAsWritten) {
    const Read r = body("auto v = Foo::create(1.0f);\nhelper::run(2);\na::b::c(3);\nEXPECT_EQ(Math::twice(2), 4);\n");
    ASSERT_EQ(r.facts.scoped_calls.size(), 4u);
    EXPECT_EQ(r.facts.scoped_calls[0].scope, std::vector<std::string>{"Foo"});
    EXPECT_EQ(r.facts.scoped_calls[0].name, "create");
    EXPECT_EQ(r.facts.scoped_calls[0].use.kind, UseKind::AutoBound);
    EXPECT_EQ(arg_types(r.facts.scoped_calls[0].args), std::vector<std::string>{"float"});
    EXPECT_EQ(r.facts.scoped_calls[2].scope, (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(r.facts.scoped_calls[3].use.kind, UseKind::Compared);
    EXPECT_EQ(hint(r.facts.scoped_calls[3].use), "int");
}

TEST(TestFrontendCalls, GlobalScopeQualifierIsDropped) {
    const Read r = body("::ns::run(1);\n");
    ASSERT_EQ(r.facts.scoped_calls.size(), 1u);
    EXPECT_EQ(r.facts.scoped_calls[0].scope, std::vector<std::string>{"ns"});
}

TEST(TestFrontendCalls, FieldReadsAndWrites) {
    const Read r = body("Foo foo;\nint n = foo.x;\nfoo.y = 5;\nEXPECT_EQ(foo.z, \"s\");\nFoo* p = &foo;\np->w = 2.5;\n");
    ASSERT_EQ(r.facts.field_accesses.size(), 4u);
    EXPECT_EQ(r.facts.field_accesses[0].field, "x");
    EXPECT_EQ(r.facts.field_accesses[0].use.kind, UseKind::Assigned);
    EXPECT_EQ(hint(r.facts.field_accesses[0].use), "int");
    EXPECT_EQ(r.facts.field_accesses[1].field, "y");
    EXPECT_EQ(r.facts.field_accesses[1].use.kind, UseKind::Assigned);
    EXPECT_EQ(hint(r.facts.field_accesses[1].use), "int");
    EXPECT_EQ(hint(r.facts.field_accesses[2].use), "std::string");
    EXPECT_TRUE(r.facts.field_accesses[3].via_arrow);
    EXPECT_EQ(hint(r.facts.field_accesses[3].use), "double");
    EXPECT_TRUE(r.facts.method_calls.empty());
}

TEST(TestFrontendCalls, FieldOfAnUnknownObjectIsNotAFact) {
    const Read r = body("int n = mystery.x;\n");
    EXPECT_TRUE(r.facts.field_accesses.empty());
}

TEST(TestFrontendCalls, CastsGiveTheirTargetType) {
    const Read r = body("Foo foo;\nfoo.set(static_cast<unsigned char>(5));\nfoo.put((long)3);\n");
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(arg_types(method(r, 0).args), std::vector<std::string>{"unsigned char"});
    EXPECT_EQ(arg_types(method(r, 1).args), std::vector<std::string>{"long"});
}

TEST(TestFrontendCalls, FunctionalCastsToBuiltinAndTypedefTypes) {
    // size_t and uint8_t are primitive types to tree-sitter, so size_t(2) is not
    // a call to a function named size_t.
    const Read r = body("Foo foo;\nfoo.put(size_t(2), uint8_t(1), int(3.5), static_cast<long>(4));\n");
    EXPECT_TRUE(r.facts.free_calls.empty());
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(arg_types(method(r).args), (std::vector<std::string>{"size_t", "uint8_t", "int", "long"}));
}

TEST(TestFrontendCalls, CastsToTheStandardLibraryAndQtHaveTheirType) {
    const Read r = body(
        "Foo foo;\n"
        "foo.put(std::string(\"x\"), std::vector<int>(3), std::size_t(2), QString(\"q\"));\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(arg_types(method(r).args), (std::vector<std::string>{"std::string", "std::vector<int>", "std::size_t", "QString"}));
    EXPECT_EQ(arg(method(r).args, 0).type->origin, ir::Origin::Declared);
}

TEST(TestFrontendCalls, TheUnqualifiedSpellingIsTheStandardTypeOnlyUnderUsingNamespaceStd) {
    const Read with = read("#include <gtest/gtest.h>\nusing namespace std;\nTEST(S, N) { Foo foo; foo.put(string(\"x\")); }\n");
    EXPECT_EQ(arg_types(method(with).args), std::vector<std::string>{"std::string"});
    const Read without = body("Foo foo;\nfoo.put(string(\"x\"));\n");
    EXPECT_FALSE(arg(method(without).args, 0).type.has_value()) << "just a function called string";
}

TEST(TestFrontendCalls, OnlyTheStdNamespaceMakesAStandardType) {
    const Read r = body("Foo foo;\nfoo.put(other::string(\"x\"), acme::vector(1), std::move_only(2));\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    for (std::size_t i = 0; i < 3; ++i) EXPECT_FALSE(arg(method(r).args, i).type.has_value()) << i;
}

TEST(TestFrontendCalls, AClassOfTheFileThatLooksLikeQtOrStdIsNotCastToTheLibraryType) {
    const Read r = read(
        "#include <gtest/gtest.h>\n"
        "class QThing { public: QThing(int); };\n"
        "TEST(S, N) { Foo foo; foo.put(QThing(1)); }\n");
    EXPECT_FALSE(arg(method(r).args, 0).type.has_value());
}

TEST(TestFrontendCalls, SizeofIsASizeT) {
    const Read r = body("Foo foo;\nfoo.reserve(sizeof(int));\n");
    EXPECT_EQ(arg_types(method(r).args), std::vector<std::string>{"std::size_t"});
}

TEST(TestFrontendCalls, NullptrAndBooleanLiterals) {
    const Read r = body("Foo foo;\nfoo.set(nullptr, true);\n");
    ASSERT_EQ(method(r).args.size(), 2u);
    EXPECT_TRUE(arg(method(r).args, 0).is_null_pointer);
    EXPECT_EQ(type_of(arg(method(r).args, 1)), "bool");
}

TEST(TestFrontendCalls, NegativeLiteralArgumentKeepsItsSignAndWarnings) {
    const Read r = body("Foo foo;\nfoo.set(-7, -2.5f);\n");
    EXPECT_EQ(arg_types(method(r).args), (std::vector<std::string>{"int", "float"}));
    EXPECT_EQ(arg(method(r).args, 0).text, "-7");
}

TEST(TestFrontendCalls, LiteralWarningsTravelWithTheValue) {
    const Read r = body("Foo foo;\nfoo.set(5000000000);\n");
    ASSERT_EQ(method(r).args.size(), 1u);
    EXPECT_EQ(type_of(arg(method(r).args, 0)), "long long");
    EXPECT_FALSE(arg(method(r).args, 0).warnings.empty());
    EXPECT_TRUE(warnings(r).empty()) << "reported only if the type is used";
}

TEST(TestFrontendCalls, LambdasAreAnalysedWithTheirParameters) {
    const Read r = body("auto check = [](Foo& f, int n) { f.run(n); };\nFoo foo;\ncheck(foo, 3);\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).receiver.name, "Foo");
    EXPECT_EQ(arg_types(method(r).args), std::vector<std::string>{"int"});
}

TEST(TestFrontendCalls, CallsInsideLoopsAndTryBlocksAreSeen) {
    const Read r = body(
        "Foo foo;\n"
        "for (int i = 0; i < 3; ++i) { foo.step(i); }\n"
        "try { foo.risky(); } catch (const MyError& e) { e.what(); }\n"
        "switch (foo.mode()) { case 1: foo.one(); break; default: foo.other(); }\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{
        "Foo::step", "Foo::risky", "MyError::what", "Foo::mode", "Foo::one", "Foo::other"}));
}

TEST(TestFrontendCalls, TernaryBranchesAreEvaluated) {
    const Read r = body("Foo foo;\nint n = true ? foo.a() : foo.b();\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::a", "Foo::b"}));
}

TEST(TestFrontendCalls, SubscriptAndOtherExpressionsAreLookedInto) {
    const Read r = body("Foo foo;\nint n = arr[foo.index()];\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).method, "index");
}

TEST(TestFrontendUse, ASwitchTestsAnIntegerNotABool) {
    const Read r = body("Foo foo;\nswitch (foo.mode()) { case 1: break; }\nif (foo.ok()) {}\n");
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Unknown);
    EXPECT_FALSE(method(r, 0).use.hint.has_value());
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Condition);
}

TEST(TestFrontendUse, DeclarationsInAConditionBindTheirValue) {
    const Read r = body(
        "if (Bar b = make()) { b.use(); }\n"
        "while (Foo* p = next()) { p->go(); }\n"
        "if (auto x = Baz(); x.ok()) {}\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Bar::use", "Foo::go"}));
    ASSERT_GE(r.facts.free_calls.size(), 2u);
    EXPECT_EQ(r.facts.free_calls[0].name, "make");
    EXPECT_EQ(r.facts.free_calls[0].use.kind, UseKind::Assigned);
    EXPECT_EQ(hint(r.facts.free_calls[0].use), "Bar");
    EXPECT_EQ(hint(r.facts.free_calls[1].use), "Foo*");
    for (const Construction& c : r.facts.constructions) EXPECT_NE(c.type.name, "Bar") << "`Bar b = make()` is not a default construction";
}

TEST(TestFrontendUse, TheInitialiserOfAnIfIsAStatementOfItsOwn) {
    const Read r = body("Foo foo;\nif (foo.prepare(); foo.ready()) {}\n");
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Discarded);
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Condition);
}

// ---- auto ---------------------------------------------------------------------------------------------------------------

TEST(TestFrontendAuto, ByValueDropsConstAndReference) {
    const Read r = body("const Foo a;\nauto b = a;\nb.x();\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).receiver.name, "Foo");
    EXPECT_FALSE(method(r).on_const) << "auto copies the object; the copy is not const";
}

TEST(TestFrontendAuto, ReferenceKeepsConstOfWhatItBindsTo) {
    const Read r = body("const Foo a;\nFoo b;\nauto& c = a;\nauto& d = b;\nconst auto& e = b;\nc.x();\nd.x();\ne.x();\n");
    ASSERT_EQ(r.facts.method_calls.size(), 3u);
    EXPECT_TRUE(method(r, 0).on_const);
    EXPECT_FALSE(method(r, 1).on_const);
    EXPECT_TRUE(method(r, 2).on_const);
}

TEST(TestFrontendAuto, PointersKeepTheConstOfThePointee) {
    const Read r = body("const Foo* p = nullptr;\nFoo* q = nullptr;\nauto a = p;\nauto b = q;\nauto* c = new Bar(1);\na->x();\nb->x();\nc->go();\n");
    ASSERT_EQ(r.facts.method_calls.size(), 3u);
    EXPECT_TRUE(method(r, 0).on_const);
    EXPECT_FALSE(method(r, 1).on_const);
    EXPECT_EQ(method(r, 2).receiver.name, "Bar");
    EXPECT_TRUE(method(r, 2).via_arrow);
}

TEST(TestFrontendAuto, MakeUniqueGivesASmartPointerOfTheClass) {
    const Read r = body("auto u = std::make_unique<Foo>(1);\nauto s = std::make_shared<ns::Bar>();\nu->a();\ns->b();\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::a", "ns::Bar::b"}));
}

TEST(TestFrontendAuto, ACallOnAVariableHoldingACallResultIsAChainedCall) {
    const Read r = body("Foo foo;\nauto x = foo.make();\nx.run();\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::make"}));
    ASSERT_EQ(r.facts.unresolved.size(), 1u);
    EXPECT_EQ(r.facts.unresolved[0].root.kind, FactRef::Kind::MethodCall);
    EXPECT_EQ(r.facts.unresolved[0].root.index, 0u);
    EXPECT_TRUE(notes(r).empty()) << "it is the chained-call gap, not an unknown variable";
}

TEST(TestFrontendAuto, WhatAnAutoVariableIsComparedWithIsEvidenceAboutTheCall) {
    const Read r = body(
        "Foo foo;\n"
        "auto n = foo.count();\n"
        "EXPECT_EQ(n, 5u);\n"
        "auto ok = foo.ready();\n"
        "EXPECT_TRUE(ok);\n"
        "auto& name = foo.name();\n"
        "EXPECT_EQ(name, \"x\");\n"
        "const auto& ratio = foo.ratio();\n"
        "EXPECT_FLOAT_EQ(ratio, 1.0f);\n");
    ASSERT_EQ(r.facts.method_calls.size(), 4u);
    for (std::size_t i = 0; i < 4; ++i) EXPECT_EQ(method(r, i).use.kind, UseKind::AutoBound) << i;
    EXPECT_EQ(hint(method(r, 0).use), "unsigned int");
    EXPECT_EQ(hint(method(r, 1).use), "bool");
    EXPECT_EQ(hint(method(r, 2).use), "std::string");
    EXPECT_EQ(hint(method(r, 3).use), "float");
    ASSERT_TRUE(method(r, 0).use.hint.has_value());
    EXPECT_NE(method(r, 0).use.hint->reason.find("through variable 'n'"), std::string::npos)
        << method(r, 0).use.hint->reason;
}

TEST(TestFrontendAuto, TheFirstHintWinsAndContextsWithoutOneAddNothing) {
    const Read r = body(
        "Foo foo;\n"
        "auto a = foo.a();\n"
        "bar(a);\n"
        "EXPECT_EQ(a, 1);\n"
        "EXPECT_EQ(a, 2.5);\n"
        "auto b = foo.b();\n"
        "bar(b);\n"
        "b.x();\n");
    ASSERT_GE(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(hint(method(r, 0).use), "int") << "the first comparison decides";
    EXPECT_FALSE(method(r, 1).use.hint.has_value());
}

TEST(TestFrontendAuto, ADeclaredVariableKeepsItsDeclaredTypeAsTheHint) {
    const Read r = body("Foo foo;\nint n = foo.count();\nEXPECT_EQ(n, 5u);\n");
    EXPECT_EQ(method(r).use.kind, UseKind::Assigned);
    EXPECT_EQ(hint(method(r).use), "int");
}

TEST(TestFrontendAuto, TwoVariablesNamedTheSameInDifferentScopesDoNotMix) {
    const Read r = body(
        "Foo foo;\n"
        "{\n"
        "    auto n = foo.a();\n"
        "    EXPECT_EQ(n, 1);\n"
        "}\n"
        "{\n"
        "    auto n = foo.b();\n"
        "    EXPECT_EQ(n, 'c');\n"
        "}\n");
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(hint(method(r, 0).use), "int");
    EXPECT_EQ(hint(method(r, 1).use), "char");
}

TEST(TestFrontendAuto, FunctionalCastIsNotMistakenForAType) {
    // `auto f = Foo(1)`: could be a function named Foo. Classification decides;
    // until then f's type is unknown and the call on it hangs from the call to Foo.
    const Read r = body("auto f = Foo(1);\nf.run();\n");
    EXPECT_TRUE(r.facts.method_calls.empty());
    ASSERT_EQ(r.facts.free_calls.size(), 1u);
    EXPECT_EQ(r.facts.free_calls[0].name, "Foo");
    ASSERT_EQ(r.facts.unresolved.size(), 1u);
    EXPECT_EQ(r.facts.unresolved[0].root.kind, FactRef::Kind::FreeCall);
    EXPECT_EQ(r.facts.unresolved[0].text, "f.run()");
}

// ---- construction by copy -----------------------------------------------------------------------------------------------

TEST(TestFrontendConstruction, CopyingAnObjectOfTheSameClassIsNotAConvertingConstructor) {
    const Read r = body("Foo a;\nFoo* p = &a;\nFoo b = a;\nFoo c = *p;\nint n = 3;\nFoo d = n;\nFoo e = true;\nFoo g = \"abc\";\n");
    // a (Default), d (Copy from int), e (Copy from bool), g (Copy from "abc")
    std::vector<std::string> forms;
    for (const Construction& c : r.facts.constructions) {
        forms.push_back(std::string(c.form == Construction::Form::Default ? "default" : "copy") + ":" +
                        (c.args.empty() ? "" : arg(c.args, 0).text));
    }
    EXPECT_EQ(forms, (std::vector<std::string>{"default:", "copy:n", "copy:true", "copy:\"abc\""}));
}

// ---- parameters, signatures and catch ----------------------------------------------------------------------------------------

TEST(TestFrontendTypes, ParametersReturnTypesCatchAndLambdasAreUses) {
    const Read r = read(
        "Foo make(const Bar& b, Baz* z) { return Foo(1); }\n"
        "TEST(S, N) {\n"
        "    try {} catch (const Err&) {}\n"
        "    auto f = [](Qux q) {};\n"
        "}\n");
    std::vector<std::string> names;
    for (const frontend::TypeUse& t : r.facts.type_uses) names.push_back(t.type.qualified());
    for (const char* expected : {"Foo", "Bar", "Baz", "Err", "Qux"}) {
        EXPECT_NE(std::find(names.begin(), names.end(), expected), names.end()) << expected;
    }
}

// ---- type uses --------------------------------------------------------------------------------------------------

TEST(TestFrontendTypes, DeclaredTypesAreUses) {
    const Read r = body("Foo a;\nns::Bar b;\nstd::vector<Baz> v;\nint n;\n");
    std::vector<std::string> names;
    for (const frontend::TypeUse& t : r.facts.type_uses) names.push_back(t.type.qualified());
    EXPECT_EQ(names, (std::vector<std::string>{"Foo", "ns::Bar", "std::vector", "Baz"}));
}

TEST(TestFrontendTypes, NewCastsAndCatchClausesAreUses) {
    const Read r = body("auto* p = new Foo(1);\nauto x = static_cast<Bar*>(q);\ntry {} catch (const Err&) {}\n");
    std::vector<std::string> names;
    for (const frontend::TypeUse& t : r.facts.type_uses) names.push_back(t.type.qualified());
    EXPECT_NE(std::find(names.begin(), names.end(), "Foo"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "Bar"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "Err"), names.end());
}

// ---- fixtures ---------------------------------------------------------------------------------------------------------

const char* const kFixture =
    "#include <gtest/gtest.h>\n"
    "class FooTest : public ::testing::Test {\n"
    "protected:\n"
    "    void SetUp() override { foo_.init(); }\n"
    "    Foo foo_;\n"
    "    const Bar bar_;\n"
    "    std::unique_ptr<Baz> baz_;\n"
    "    int helper(int x);\n"
    "};\n"
    "int FooTest::helper(int x) { return foo_.twice(x); }\n";

TEST(TestFrontendFixture, TestFSeesTheMembersOfItsFixture) {
    const Read r = read(std::string(kFixture) +
                        "TEST_F(FooTest, Runs) {\n"
                        "    foo_.run(1);\n"
                        "    EXPECT_EQ(bar_.size(), 2u);\n"
                        "    baz_->go();\n"
                        "}\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{
        "Foo::init", "Foo::twice", "Foo::run", "Bar::size", "Baz::go"}));
    EXPECT_TRUE(method(r, 3).on_const);
    EXPECT_TRUE(notes(r).empty());
}

TEST(TestFrontendFixture, FixtureMethodsAreAnalysedInlineAndOutOfClass) {
    const Read r = read(kFixture);
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::init", "Foo::twice"}));
    EXPECT_EQ(arg_types(method(r, 1).args), std::vector<std::string>{"int"}) << "the parameter x";
}

TEST(TestFrontendFixture, TheFixtureItselfIsNotAConstruction) {
    const Read r = read(std::string(kFixture) + "TEST_F(FooTest, Runs) {}\n");
    for (const Construction& c : r.facts.constructions) EXPECT_NE(c.type.name, "FooTest");
    ASSERT_EQ(r.facts.local.classes.size(), 1u);
    EXPECT_EQ(r.facts.local.classes[0].name, "FooTest");
}

TEST(TestFrontendFixture, MembersOfBaseFixturesAreVisible) {
    const Read r = read(
        "class Base : public ::testing::Test { protected: Foo shared_; };\n"
        "class Derived : public Base { protected: Bar own_; };\n"
        "TEST_F(Derived, Both) { shared_.a(); own_.b(); }\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::a", "Bar::b"}));
}

TEST(TestFrontendFixture, MembersOfAnUnrelatedFixtureAreNotVisible) {
    const Read r = read(
        "class A : public ::testing::Test { protected: Foo only_a_; };\n"
        "class B : public ::testing::Test { protected: Bar only_b_; };\n"
        "TEST_F(B, T) { only_a_.x(); only_b_.y(); }\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Bar::y"}));
    EXPECT_EQ(notes(r), (std::vector<std::string>{"mt-unknown-receiver"}));
}

TEST(TestFrontendFixture, LocalsShadowMembers) {
    const Read r = read(
        "class F : public ::testing::Test { protected: Foo x_; };\n"
        "TEST_F(F, T) { Bar x_; x_.a(); }\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Bar::a"}));
}

TEST(TestFrontendFixture, UnknownFixtureIsAWarningAndTheBodyIsStillRead) {
    const Read r = read("TEST_F(Missing, T) { Foo f; f.run(); mystery.go(); }\n");
    EXPECT_EQ(warnings(r), (std::vector<std::string>{"mt-unknown-fixture"}));
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::run"}));
    ASSERT_EQ(r.facts.tests.size(), 1u);
    EXPECT_EQ(r.facts.tests[0].suite, "Missing");
}

TEST(TestFrontendFixture, FixtureConstructorAndDestructorBodiesAreAnalysed) {
    const Read r = read(
        "class F : public ::testing::Test {\n"
        "public:\n"
        "    F() { foo_.setup(); }\n"
        "    ~F() { foo_.teardown(); }\n"
        "    Foo foo_;\n"
        "};\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::setup", "Foo::teardown"}));
}

TEST(TestFrontendFixture, TestPUsesGetParamTypedByTestWithParam) {
    const Read r = read(
        "class P : public ::testing::TestWithParam<unsigned> { protected: Foo foo_; };\n"
        "TEST_P(P, Runs) { EXPECT_EQ(foo_.size(), GetParam()); }\n");
    ASSERT_EQ(r.facts.tests.size(), 1u);
    EXPECT_EQ(r.facts.tests[0].kind, "TEST_P");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(hint(method(r).use), "unsigned");
}

TEST(TestFrontendFixture, GetParamFromWithParamInterfaceAndStructParameters) {
    const Read r = read(
        "class P : public Base, public ::testing::WithParamInterface<std::pair<int, std::string>> { Foo foo_; };\n"
        "TEST_P(P, Runs) { EXPECT_EQ(foo_.value(), GetParam()); }\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(hint(method(r).use), "std::pair<int, std::string>");
}

TEST(TestFrontendFixture, GetParamWithoutAParameterTypeIsAnOrdinaryCall) {
    const Read r = body("EXPECT_EQ(GetParam(), 3);\n");
    ASSERT_EQ(r.facts.free_calls.size(), 1u);
    EXPECT_EQ(r.facts.free_calls[0].name, "GetParam");
}

TEST(TestFrontendFixture, FixtureMethodsDefinedAfterTheTestsAreStillRead) {
    const Read r = read(
        "class F : public ::testing::Test { protected: Foo foo_; void check(); };\n"
        "TEST_F(F, T) { check(); }\n"
        "void F::check() { foo_.verify(); }\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::verify"}));
    EXPECT_EQ(r.facts.free_calls.size(), 1u);
}

TEST(TestFrontendFixture, HelperFunctionsSeeTheirParameters) {
    const Read r = read(
        "static void check(const Foo& f, Bar* b, int n) { EXPECT_EQ(f.size(), n); b->go(); }\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::size", "Bar::go"}));
    EXPECT_TRUE(method(r, 0).on_const);
    EXPECT_EQ(hint(method(r, 0).use), "int");
}

TEST(TestFrontendFixture, StructFixturesWork) {
    const Read r = read(
        "struct S : ::testing::Test { Foo foo; };\n"
        "TEST_F(S, T) { foo.go(); }\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::go"}));
}

TEST(TestFrontendFixture, FixtureInANamespaceIsFoundFromTheTest) {
    const Read r = read(
        "namespace t {\n"
        "class F : public ::testing::Test { protected: Foo foo_; };\n"
        "TEST_F(F, T) { foo_.go(); }\n"
        "}\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::go"}));
}

// ---- what a fixture's members imply --------------------------------------------------------------------------------------------------

TEST(TestFrontendFixture, ByValueMembersOfClassTypeAreConstructedWithTheFixture) {
    const Read r = read(
        "class F : public ::testing::Test {\n"
        "public:\n"
        "    Foo a_;\n"
        "    Baz c_{1, 2};\n"
        "    Qux d_ = Qux(2);\n"
        "    Foo* p_ = nullptr;\n"
        "    Foo& r_ = a_;\n"
        "    std::unique_ptr<Baz> u_;\n"
        "    static Foo shared_;\n"
        "    int n_ = 0;\n"
        "};\n");
    std::vector<std::string> made;
    for (const Construction& c : r.facts.constructions) {
        const char* form = c.form == Construction::Form::Default      ? "default"
                         : c.form == Construction::Form::Brace        ? "brace"
                         : c.form == Construction::Form::Functional   ? "functional"
                                                                      : "other";
        made.push_back(c.type.qualified() + ":" + form + ":" + std::to_string(c.args.size()));
    }
    EXPECT_EQ(made, (std::vector<std::string>{"Foo:default:0", "Baz:brace:2", "Qux:functional:1",
                                              "std::unique_ptr:default:0"}));
}

TEST(TestFrontendFixture, ConstructorInitialisersConstructTheirMembers) {
    const Read r = read(
        "class F : public ::testing::Test {\n"
        "public:\n"
        "    F(int n) : a_(n, 2), b_{4}, count_(compute()), Base(7) {}\n"
        "    Foo a_;\n"
        "    Bar b_;\n"
        "    int count_;\n"
        "};\n"
        "F::F() : a_(1) {}\n");
    std::vector<std::string> made;
    for (const Construction& c : r.facts.constructions) {
        if (c.form == Construction::Form::Default) continue;
        made.push_back(c.type.name + ":" + (c.form == Construction::Form::Direct ? "direct" : "brace") + ":" +
                       std::to_string(c.args.size()));
    }
    EXPECT_EQ(made, (std::vector<std::string>{"Foo:direct:2", "Bar:brace:1", "Foo:direct:1"}));
    ASSERT_EQ(r.facts.free_calls.size(), 1u);
    EXPECT_EQ(r.facts.free_calls[0].name, "compute");
    EXPECT_EQ(r.facts.free_calls[0].use.kind, UseKind::Assigned);
    EXPECT_EQ(hint(r.facts.free_calls[0].use), "int");
}

TEST(TestFrontendFixture, AMemberTheConstructorInitialisesIsNotAlsoDefaultConstructed) {
    const Read r = read(
        "class F : public ::testing::Test {\n"
        "public:\n"
        "    F() : a_(1), b_{2} {}\n"
        "    Foo a_;\n"
        "    Bar b_;\n"
        "    Baz c_;\n"
        "    Qux d_ = Qux(2);\n"
        "};\n");
    std::vector<std::string> made;
    for (const Construction& c : r.facts.constructions) {
        made.push_back(c.type.name + ":" + (c.form == Construction::Form::Default ? "default" : "other"));
    }
    // a_ and b_ are built by the constructor; c_ by nobody, so by default.
    EXPECT_EQ(made, (std::vector<std::string>{"Foo:other", "Bar:other", "Baz:default", "Qux:other"}));
}

TEST(TestFrontendFixture, TheInitialisersOfOneClassDoNotLeakIntoTheNext) {
    const Read r = read(
        "class A : public ::testing::Test {\n"
        "public:\n"
        "    A() : x_(1) {}\n"
        "    Foo x_;\n"
        "};\n"
        "class B : public ::testing::Test {\n"
        "public:\n"
        "    Foo x_;\n"
        "};\n");
    std::vector<std::string> made;
    for (const Construction& c : r.facts.constructions) {
        made.push_back(c.type.name + ":" + (c.form == Construction::Form::Default ? "default" : "other"));
    }
    EXPECT_EQ(made, (std::vector<std::string>{"Foo:other", "Foo:default"}));
}

TEST(TestFrontendFixture, ConstructorParametersAreVisibleToTheInitialisers) {
    const Read r = read(
        "class F : public ::testing::Test {\n"
        "public:\n"
        "    F(const Foo& seed) : count_(seed.size()) {}\n"
        "    int count_;\n"
        "};\n");
    ASSERT_EQ(r.facts.method_calls.size(), 1u);
    EXPECT_EQ(method(r).receiver.name, "Foo");
    EXPECT_TRUE(method(r).on_const);
    EXPECT_EQ(hint(method(r).use), "int");
}

// ---- determinism ----------------------------------------------------------------------------------------------------

TEST(TestFrontendDeterminism, ReadingTwiceGivesTheSameFacts) {
    const std::string text = std::string(kFixture) +
        "TEST_F(FooTest, A) { foo_.run(1); EXPECT_EQ(bar_.size(), 2u); Foo f(1, \"x\"); }\n";
    const Read a = read(text);
    const Read b = read(text);
    ASSERT_EQ(a.facts.method_calls.size(), b.facts.method_calls.size());
    for (std::size_t i = 0; i < a.facts.method_calls.size(); ++i) {
        EXPECT_EQ(a.facts.method_calls[i].method, b.facts.method_calls[i].method);
        EXPECT_EQ(a.facts.method_calls[i].where.line, b.facts.method_calls[i].where.line);
        EXPECT_EQ(hint(a.facts.method_calls[i].use), hint(b.facts.method_calls[i].use));
    }
    EXPECT_EQ(a.diagnostics.sorted(), b.diagnostics.sorted());
}

TEST(TestFrontendDeterminism, LocationsPointAtTheMethodName) {
    const Read r = body("Foo foo;\n  foo.run();\n");
    EXPECT_EQ(method(r).where.file, "t.cpp");
    EXPECT_EQ(method(r).where.line, 5u);
    EXPECT_EQ(method(r).where.column, 7u);
}

// ---- names the file itself brings ------------------------------------------------------------------------------------------

TEST(TestFrontendNames, TemplateParametersAreNotTypesOfTheCodeUnderTest) {
    const Read r = read(
        "template <typename T, int N>\n"
        "void check(const T& x) { T copy = x; Foo f; }\n"
        "template <typename U, typename... Rest>\n"
        "struct Holder { U u; Bar b; };\n"
        "TEST(S, N) { Qux q; }\n");
    std::vector<std::string> names;
    for (const frontend::TypeUse& t : r.facts.type_uses) names.push_back(t.type.qualified());
    EXPECT_EQ(names, (std::vector<std::string>{"Foo", "Bar", "Qux"}));
    ASSERT_EQ(r.facts.tests.size(), 1u);
}

TEST(TestFrontendNames, ATemplateFunctionBodyIsAnalysedAndItsTemplateScopeEnds) {
    const Read r = read(
        "template <typename T>\n"
        "void check(Foo& foo, const T& x) { foo.set(x); }\n"
        "TEST(S, N) { T t; }\n");
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::set"}));
    std::vector<std::string> names;
    for (const frontend::TypeUse& t : r.facts.type_uses) names.push_back(t.type.qualified());
    EXPECT_EQ(names, (std::vector<std::string>{"Foo", "T"})) << "T is only a template parameter inside the template";
}

TEST(TestFrontendNames, UsingDeclarationsAndAliasesAreRecorded) {
    const Read r = read(
        "using std::string;\n"
        "using acme::Foo;\n"
        "using namespace acme::detail;\n"
        "using Vec = std::vector<Bar>;\n"
        "typedef unsigned long Word;\n"
        "void g() { using Local = Baz; typedef int I; Local l; }\n");
    ASSERT_EQ(r.facts.using_declarations.size(), 2u);
    EXPECT_EQ(r.facts.using_declarations[0].qualified(), "std::string");
    EXPECT_EQ(r.facts.using_declarations[1].qualified(), "acme::Foo");
    EXPECT_EQ(r.facts.using_namespaces, std::vector<std::string>{"acme::detail"});
    EXPECT_EQ(r.facts.local_type_names, (std::vector<std::string>{"Vec", "Word", "Local", "I"}));

    std::vector<std::string> names;
    for (const frontend::TypeUse& t : r.facts.type_uses) names.push_back(t.type.qualified());
    EXPECT_NE(std::find(names.begin(), names.end(), "Bar"), names.end()) << "the aliased type is a use";
    EXPECT_NE(std::find(names.begin(), names.end(), "Baz"), names.end());
}

TEST(TestFrontendNames, CallsThroughLocalVariablesAreNotFreeCalls) {
    const Read r = body(
        "auto lam = [](int x) { return x; };\n"
        "int (*fp)(int) = nullptr;\n"
        "std::function<int(int)> fn;\n"
        "lam(1);\nfp(2);\nfn(3);\n");
    EXPECT_TRUE(r.facts.free_calls.empty());
    EXPECT_TRUE(r.facts.method_calls.empty());
}

TEST(TestFrontendNames, CallingAnObjectOfAClassIsItsCallOperator) {
    const Read r = body("Adder add;\nconst Adder cadd;\nEXPECT_EQ(add(1, 2), 3);\ncadd(4);\n");
    ASSERT_EQ(r.facts.method_calls.size(), 2u);
    EXPECT_EQ(method(r, 0).receiver.name, "Adder");
    EXPECT_EQ(method(r, 0).method, "operator()");
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Compared);
    EXPECT_EQ(arg_types(method(r, 0).args), (std::vector<std::string>{"int", "int"}));
    EXPECT_TRUE(method(r, 1).on_const);
    EXPECT_TRUE(r.facts.free_calls.empty());
}

TEST(TestFrontendNames, AFunctionOfTheFileWithTheSameNameAsALocalIsTheLocal) {
    const Read r = body("int twice = 3;\ntwice(2);\n");
    EXPECT_TRUE(r.facts.free_calls.empty());
}

TEST(TestFrontendNames, FreeCallsRememberTheFixtureTheyAreIn) {
    const Read r = read(
        "class F : public ::testing::Test {\n"
        "protected:\n"
        "    void check() { helper(1); this->other(); }\n"
        "};\n"
        "TEST_F(F, T) { helper(2); }\n"
        "TEST(S, N) { helper(3); }\n"
        "void util() { helper(4); }\n");
    ASSERT_EQ(r.facts.free_calls.size(), 5u);
    EXPECT_EQ(r.facts.free_calls[0].owner, "F");
    EXPECT_EQ(r.facts.free_calls[1].owner, "F");
    EXPECT_EQ(r.facts.free_calls[2].owner, "F") << "TEST_F bodies are methods of the fixture";
    EXPECT_EQ(r.facts.free_calls[3].owner, "");
    EXPECT_EQ(r.facts.free_calls[4].owner, "");
}

TEST(TestFrontendNames, UnqualifiedMakeUniqueAndMoveUnderUsingNamespaceStd) {
    const Read r = read(
        "using namespace std;\n"
        "TEST(S, N) {\n"
        "    auto p = make_unique<Foo>(1);\n"
        "    auto s = make_shared<Bar>();\n"
        "    Baz baz(move(p));\n"
        "    p->go();\n"
        "}\n");
    ASSERT_EQ(r.facts.constructions.size(), 3u);
    EXPECT_EQ(construction(r, 0).form, Construction::Form::MakeUnique);
    EXPECT_EQ(construction(r, 0).type.name, "Foo");
    EXPECT_EQ(construction(r, 1).type.name, "Bar");
    EXPECT_EQ(construction(r, 2).type.name, "Baz");
    EXPECT_TRUE(arg(construction(r, 2).args, 0).is_moved);
    EXPECT_TRUE(r.facts.free_calls.empty());
    EXPECT_EQ(methods_called(r), (std::vector<std::string>{"Foo::go"}));
}

TEST(TestFrontendNames, MoveWithoutUsingNamespaceStdIsAnOrdinaryCall) {
    const Read r = body("Baz baz(move(p));\n");
    ASSERT_EQ(r.facts.free_calls.size(), 1u);
    EXPECT_EQ(r.facts.free_calls[0].name, "move");
}

TEST(TestFrontendNames, AFunctionTemplateCallIsReportedNotGuessed) {
    const Read r = body("int n = compute<int>(3);\n");
    EXPECT_EQ(warnings(r), (std::vector<std::string>{"mt-unsupported"}));
    EXPECT_TRUE(r.facts.free_calls.empty());
}

TEST(TestFrontendChain, TheCallOnTheUnknownResultWaitsForAReceiver) {
    const Read r = body("Foo foo;\nEXPECT_EQ(foo.make()->run(1, 'c'), 3u);\n");
    ASSERT_EQ(r.facts.unresolved.size(), 1u);
    const frontend::MethodCall& pending = r.facts.unresolved[0].call;
    EXPECT_TRUE(pending.receiver.empty());
    EXPECT_EQ(pending.method, "run");
    EXPECT_TRUE(pending.via_arrow);
    EXPECT_EQ(arg_types(pending.args), (std::vector<std::string>{"int", "char"}));
    EXPECT_EQ(pending.use.kind, UseKind::Compared) << "the comparison reaches the pending call, as for any call";
    EXPECT_EQ(hint(pending.use), "unsigned int");
}

TEST(TestFrontendChain, AnAutoVariableOfAnUnresolvedCallAlsoLandsOnIt) {
    const Read r = body("Foo foo;\nauto n = foo.make().count();\nEXPECT_EQ(n, 5);\n");
    ASSERT_EQ(r.facts.unresolved.size(), 1u);
    EXPECT_EQ(r.facts.unresolved[0].call.use.kind, UseKind::AutoBound);
    EXPECT_EQ(hint(r.facts.unresolved[0].call.use), "int");
}

// ---- a realistic file, and files cut off anywhere ---------------------------------------------------------------------------

Read read_data(const char* name) {
    return read(mt::test::read_file(std::string(MT_TEST_DATA_DIR) + "/" + name));
}

TEST(TestFrontendSpike, TheParserSpikeFileYieldsTheFactsItWasWrittenToExercise) {
    const Read r = read_data("spike_gtest.cpp");

    ASSERT_EQ(r.facts.tests.size(), 3u);
    EXPECT_EQ(r.facts.tests[0].suite + "." + r.facts.tests[0].name, "FooTest.SizeStartsAtThree");
    EXPECT_EQ(r.facts.tests[1].kind, "TEST_F");
    EXPECT_EQ(r.facts.tests[2].kind, "TEST_P");
    ASSERT_EQ(r.facts.local.classes.size(), 1u);
    EXPECT_EQ(r.facts.local.classes[0].name, "FooFixture");
    EXPECT_EQ(r.facts.includes.size(), 2u);

    // TEST_P(FooParam, ...) names a fixture the file does not define; the vexing
    // `Foo f2();` is reported. Nothing else is worth a warning.
    EXPECT_EQ(warnings(r), (std::vector<std::string>{"mt-vexing-parse", "mt-unknown-fixture"}));

    EXPECT_EQ(methods_called(r), (std::vector<std::string>{
        "Foo::size", "Foo::empty", "Foo::name", "Foo::count", "Foo::count", "Foo::run", "Foo::parse",
        "Foo::run", "Foo::reset", "Foo::items", "Foo::size", "Foo::ratio"}));

    // The fixture's `Foo foo_{3};` is a construction with one argument, and the
    // reference member `const Foo& cfoo_` constructs nothing.
    std::vector<std::string> made;
    for (const Construction& c : r.facts.constructions) {
        made.push_back(c.type.qualified() + "/" + std::to_string(c.args.size()));
    }
    EXPECT_EQ(made, (std::vector<std::string>{"Foo/1", "Foo/2", "ns::Bar/0", "Foo/1"}));

    // One of each use, as the spike intends.
    EXPECT_EQ(method(r, 0).use.kind, UseKind::Compared);
    EXPECT_EQ(hint(method(r, 0).use), "unsigned int");
    EXPECT_EQ(method(r, 1).use.kind, UseKind::Condition);
    EXPECT_EQ(hint(method(r, 2).use), "const char*");
    EXPECT_EQ(method(r, 3).use.kind, UseKind::Assigned);
    EXPECT_EQ(method(r, 4).use.kind, UseKind::AutoBound);
    EXPECT_EQ(method(r, 5).use.kind, UseKind::Discarded);
    EXPECT_EQ(method(r, 6).use.kind, UseKind::Discarded);
    EXPECT_EQ(hint(method(r, 9).use), "<none>") << "compared with a variable of unknown type";
    EXPECT_TRUE(method(r, 10).on_const) << "cfoo_ is a const reference";
    EXPECT_EQ(hint(method(r, 10).use), "int");
    EXPECT_EQ(hint(method(r, 11).use), "double");

    ASSERT_EQ(r.facts.scoped_calls.size(), 1u);
    EXPECT_EQ(r.facts.scoped_calls[0].scope, std::vector<std::string>{"Foo"});
}

TEST(TestFrontendSpike, EveryPrefixOfARealisticFileIsReadWithoutIncident) {
    // Truncating anywhere produces trees full of ERROR and MISSING nodes. The
    // result may be incomplete, never a crash (this runs under the sanitizers in CI).
    for (const char* name : {"spike_gtest.cpp", "spike_qtest.cpp"}) {
        const std::string text = mt::test::read_file(std::string(MT_TEST_DATA_DIR) + "/" + name);
        for (std::size_t n = 0; n <= text.size(); ++n) {
            const Read r = read(text.substr(0, n));
            EXPECT_LE(r.facts.tests.size(), 8u) << name << " cut at " << n;
        }
    }
}

TEST(TestFrontendSpike, ReadingTheSpikeTwiceFormatsTheSameDiagnostics) {
    const Read a = read_data("spike_gtest.cpp");
    const Read b = read_data("spike_gtest.cpp");
    std::ostringstream x, y;
    a.diagnostics.format(x);
    b.diagnostics.format(y);
    EXPECT_EQ(x.str(), y.str());
    EXPECT_NE(x.str().find("t.cpp:"), std::string::npos);
}

// ---- type parsing --------------------------------------------------------------------------------------------------------

TEST(TypeName, ParsesScopesAndTemplateArguments) {
    EXPECT_EQ(frontend::parse_type_name("Foo").name, "Foo");
    EXPECT_TRUE(frontend::parse_type_name("Foo").scope.empty());

    const frontend::TypeName a = frontend::parse_type_name("::a::b::Foo");
    EXPECT_EQ(a.scope, (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(a.name, "Foo");
    EXPECT_EQ(a.qualified(), "a::b::Foo");

    const frontend::TypeName t = frontend::parse_type_name("std::map<int, std::vector<ns::X>>");
    EXPECT_EQ(t.scope, std::vector<std::string>{"std"});
    EXPECT_EQ(t.name, "map");
    EXPECT_EQ(t.template_args, "int, std::vector<ns::X>");
    EXPECT_EQ(t.qualified(), "std::map");
}

TEST(TypeName, TemplateArgumentsBelongToTheLastSegmentAndTheirScopesAreNobodys) {
    const frontend::TypeName a = frontend::parse_type_name("Box<std::string>::create");
    EXPECT_EQ(a.scope, std::vector<std::string>{"Box"});
    EXPECT_EQ(a.name, "create");
    EXPECT_TRUE(a.template_args.empty());

    const frontend::TypeName b = frontend::parse_type_name("ns::Box<a::B, c::D<e::F>>");
    EXPECT_EQ(b.scope, std::vector<std::string>{"ns"});
    EXPECT_EQ(b.name, "Box");
    EXPECT_EQ(b.template_args, "a::B, c::D<e::F>");

    const frontend::TypeName c = frontend::parse_type_name("std::vector<int>::iterator");
    EXPECT_EQ(c.scope, (std::vector<std::string>{"std", "vector"}));
    EXPECT_EQ(c.name, "iterator");
}

TEST(TypeName, EmptyAndOddInputsDoNotCrash) {
    EXPECT_TRUE(frontend::parse_type_name("").empty());
    EXPECT_EQ(frontend::parse_type_name("a<").name, "a");
    EXPECT_EQ(frontend::parse_type_name(">>").name, ">>");
}

}  // namespace
