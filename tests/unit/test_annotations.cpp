// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Two halves: reading the comments (syntax, targets, errors) and applying the
// short directives to the calls of a classified file.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/frontend/classify.h"
#include "mirrortest/frontend/test_frontend.h"

using namespace mt;

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

struct Outcome {
    frontend::Classified result;
    core::Diagnostics    diagnostics;
};

Outcome classify(const std::string& text, frontend::ClassifyOptions options = {}) {
    core::Diagnostics ignored;
    frontend::TestFileFacts facts = frontend::read_test_file({"t.cpp", text}, {}, ignored);
    Outcome o;
    // read_test_file's own annotation errors are part of what these tests check.
    o.diagnostics = ignored;
    o.result = frontend::classify(std::move(facts), options, o.diagnostics);
    return o;
}

Outcome body(const std::string& statements, frontend::ClassifyOptions options = {}) {
    return classify("#include <gtest/gtest.h>\nTEST(S, N) {\n" + statements + "\n}\n", std::move(options));
}

std::vector<std::string> errors(const core::Diagnostics& d) {
    std::vector<std::string> out;
    for (const core::Diagnostic& x : d.sorted()) {
        if (x.severity == core::Severity::Error) out.push_back(x.code);
    }
    return out;
}

std::vector<std::string> messages(const core::Diagnostics& d) {
    std::vector<std::string> out;
    for (const core::Diagnostic& x : d.sorted()) {
        if (x.severity == core::Severity::Error) out.push_back(x.message);
    }
    return out;
}

bool mentions(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& l : lines) {
        if (l.find(text) != std::string::npos) return true;
    }
    return false;
}

const std::vector<std::string> kOneError = {"mt-annotation"};

// ---- the directives ----------------------------------------------------------------------------------------

TEST(AnnotationRead, TheShortDirectives) {
    const Read r = read(
        "void f() {\n"
        "    // @mt: returns std::vector<int>\n"
        "    a();\n"
        "    // @mt: overload\n"
        "    b();\n"
        "    // @mt: ignore\n"
        "    c();\n"
        "    // @mt: returns int; overload\n"
        "    d();\n"
        "}\n");
    EXPECT_TRUE(r.diagnostics.empty());
    ASSERT_EQ(r.facts.annotations.shorts.size(), 4u);
    const auto& s = r.facts.annotations.shorts;
    ASSERT_TRUE(s[0].returns.has_value());
    EXPECT_EQ(s[0].returns->spelling, "std::vector<int>");
    EXPECT_EQ(s[0].returns->origin, ir::Origin::Annotated);
    EXPECT_FALSE(s[0].overload);
    EXPECT_TRUE(s[1].overload);
    EXPECT_FALSE(s[1].returns.has_value());
    EXPECT_TRUE(s[2].ignore);
    ASSERT_TRUE(s[3].returns.has_value());
    EXPECT_TRUE(s[3].overload) << "several directives of one comment share a target";
    EXPECT_EQ(s[0].where.line, 2u);
    EXPECT_EQ(s[0].where.column, 5u);
}

TEST(AnnotationRead, TypesAreNormalised) {
    const Read r = read("void f() {\n// @mt: returns   std::map< int ,  std::string >  \nx();\n}\n");
    ASSERT_EQ(r.facts.annotations.shorts.size(), 1u);
    EXPECT_EQ(r.facts.annotations.shorts[0].returns->spelling, "std::map<int, std::string>");
}

TEST(AnnotationRead, TheLongDirectives) {
    const Read r = read(
        "// @mt: acme::Foo::items returns std::vector<int>\n"
        "// @mt: acme::Foo::set param 1 unsigned int; acme::Foo::set param 2 const char*\n"
        "// @mt: Foo::size const; Foo::create static; Foo::run virtual; Foo::stop noexcept\n"
        "// @mt: ::top::Bar::go const\n");
    EXPECT_TRUE(r.diagnostics.empty());
    const auto& o = r.facts.annotations.overrides;
    ASSERT_EQ(o.count("acme::Foo::items"), 1u);
    EXPECT_EQ(o.at("acme::Foo::items").returns, std::optional<std::string>("std::vector<int>"));
    ASSERT_EQ(o.count("acme::Foo::set"), 1u);
    EXPECT_EQ(o.at("acme::Foo::set").params.at(1), "unsigned int");
    EXPECT_EQ(o.at("acme::Foo::set").params.at(2), "const char*");
    EXPECT_EQ(o.at("Foo::size").is_const, std::optional<bool>(true));
    EXPECT_EQ(o.at("Foo::create").is_static, std::optional<bool>(true));
    EXPECT_EQ(o.at("Foo::run").is_virtual, std::optional<bool>(true));
    EXPECT_EQ(o.at("Foo::stop").is_noexcept, std::optional<bool>(true));
    EXPECT_EQ(o.count("top::Bar::go"), 1u) << "a leading :: is dropped";
    EXPECT_TRUE(r.facts.annotations.shorts.empty()) << "long forms are about no statement";
}

TEST(AnnotationRead, SeveralAttributesOfOneMemberMerge) {
    const Read r = read("// @mt: Foo::f returns int; Foo::f const; Foo::f param 1 long\n");
    EXPECT_TRUE(r.diagnostics.empty());
    const core::MemberOverride& m = r.facts.annotations.overrides.at("Foo::f");
    EXPECT_EQ(m.returns, std::optional<std::string>("int"));
    EXPECT_EQ(m.is_const, std::optional<bool>(true));
    EXPECT_EQ(m.params.at(1), "long");
}

TEST(AnnotationRead, OperatorsAreMembers) {
    const Read r = read("// @mt: Foo::operator== const; Foo::operator() returns int\n");
    EXPECT_TRUE(r.diagnostics.empty());
    EXPECT_EQ(r.facts.annotations.overrides.count("Foo::operator=="), 1u);
    EXPECT_EQ(r.facts.annotations.overrides.count("Foo::operator()"), 1u);
}

TEST(AnnotationRead, TheClassDirectives) {
    const Read r = read("// @mt: class Foo namespace acme::detail; class Foo header \"acme/foo.h\"\n"
                        "// @mt: class Bar header \"a;b.h\"\n");
    EXPECT_TRUE(r.diagnostics.empty());
    EXPECT_EQ(r.facts.annotations.class_namespace.at("Foo"), "acme::detail");
    EXPECT_EQ(r.facts.annotations.class_header.at("Foo"), "acme/foo.h");
    EXPECT_EQ(r.facts.annotations.class_header.at("Bar"), "a;b.h") << "a ';' inside quotes does not end the directive";
}

TEST(AnnotationRead, TrailingSemicolonAndSpacingAreFine) {
    const Read r = read("//@mt:Foo::f const;\n//   @mt:    Foo::g const  ;  \n");
    EXPECT_TRUE(r.diagnostics.empty());
    EXPECT_EQ(r.facts.annotations.overrides.size(), 2u);
}

TEST(AnnotationRead, OrdinaryCommentsAndStringsAreNotAnnotations) {
    const Read r = read(
        "// just a comment mentioning @mt: in passing\n"
        "// mt: returns int\n"
        "// @mtx returns int\n"
        "const char* s = \"// @mt: returns int\";\n"
        "/* a block comment */\n");
    EXPECT_TRUE(r.diagnostics.empty());
    EXPECT_TRUE(r.facts.annotations.shorts.empty());
    EXPECT_TRUE(r.facts.annotations.overrides.empty());
}

// ---- mistakes ------------------------------------------------------------------------------------------------------

TEST(AnnotationErrors, ATypoInThePrefixIsAnErrorNotSilence) {
    EXPECT_EQ(errors(read("// @mt returns int\nint x;\n").diagnostics), kOneError);
    const Read block = read("/* @mt: returns int */\nint x;\n");
    EXPECT_EQ(errors(block.diagnostics), kOneError);
    EXPECT_TRUE(mentions(messages(block.diagnostics), "line comment"));
}

TEST(AnnotationErrors, EmptyAnnotation) {
    const Read r = read("// @mt:\nint x;\n// @mt:   ;  \nint y;\n");
    EXPECT_EQ(errors(r.diagnostics), kOneError) << "the empty one; a lone ';' is just an empty list";
}

TEST(AnnotationErrors, UnknownDirective) {
    const Read r = read("// @mt: frobnicate int\nint x;\n");
    EXPECT_EQ(errors(r.diagnostics), kOneError);
    EXPECT_TRUE(mentions(messages(r.diagnostics), "unknown annotation 'frobnicate'"));
}

TEST(AnnotationErrors, ReturnsNeedsAType) {
    EXPECT_EQ(errors(read("// @mt: returns\nint x;\n").diagnostics), kOneError);
    EXPECT_EQ(errors(read("// @mt: Foo::f returns\n").diagnostics), kOneError);
    EXPECT_EQ(errors(read("// @mt: returns int; returns long\nint x;\n").diagnostics), kOneError) << "twice in one comment";
}

TEST(AnnotationErrors, FlagsTakeNothingMore) {
    EXPECT_EQ(errors(read("// @mt: overload now\nint x;\n").diagnostics), kOneError);
    EXPECT_EQ(errors(read("// @mt: ignore this\nint x;\n").diagnostics), kOneError);
    EXPECT_EQ(errors(read("// @mt: Foo::f const please\n").diagnostics), kOneError);
}

TEST(AnnotationErrors, ClassDirectives) {
    for (const char* text : {"// @mt: class\n", "// @mt: class Foo\n", "// @mt: class Foo namespace\n",
                             "// @mt: class Foo namespace 1bad\n", "// @mt: class Foo header acme/foo.h\n",
                             "// @mt: class Foo header \"\"\n", "// @mt: class Foo header \"a\"b\"\n",
                             "// @mt: class acme::Foo namespace x\n", "// @mt: class Foo colour red\n"}) {
        EXPECT_EQ(errors(read(text).diagnostics), kOneError) << text;
    }
}

TEST(AnnotationErrors, MemberDirectives) {
    for (const char* text : {"// @mt: Foo::\n", "// @mt: ::f returns int\n", "// @mt: Foo::f\n",
                             "// @mt: Foo::f frobnicate\n", "// @mt: 1Foo::f const\n", "// @mt: Foo::f param\n",
                             "// @mt: Foo::f param 0 int\n", "// @mt: Foo::f param x int\n",
                             "// @mt: Foo::f param 01 int\n", "// @mt: Foo::f param 1\n",
                             "// @mt: Foo::f param 99999 int\n", "// @mt: Foo::f param -1 int\n"}) {
        EXPECT_EQ(errors(read(text).diagnostics), kOneError) << text;
    }
}

TEST(AnnotationErrors, TheSameThingTwiceIsNotLeftToChance) {
    const Read r = read(
        "// @mt: Foo::f returns int\n"
        "// @mt: Foo::f returns long\n"
        "// @mt: Foo::g const; Foo::g const\n"
        "// @mt: Foo::h param 1 int\n"
        "// @mt: Foo::h param 1 long\n"
        "// @mt: Foo::h param 2 long\n"
        "// @mt: class A namespace x\n"
        "// @mt: class A namespace y\n");
    EXPECT_EQ(errors(r.diagnostics), (std::vector<std::string>{"mt-annotation", "mt-annotation", "mt-annotation", "mt-annotation"}));
    EXPECT_EQ(r.facts.annotations.overrides.at("Foo::f").returns, std::optional<std::string>("int")) << "the first stands";
    EXPECT_EQ(r.facts.annotations.overrides.at("Foo::h").params.at(2), "long");
    EXPECT_EQ(r.facts.annotations.class_namespace.at("A"), "x");
    EXPECT_TRUE(mentions(messages(r.diagnostics), "already annotated at line 1"));
}

TEST(AnnotationErrors, GoodDirectivesSurviveBadOnes) {
    const Read r = read("// @mt: Foo::f const; bogus; Foo::g const\n");
    EXPECT_EQ(errors(r.diagnostics), kOneError);
    EXPECT_EQ(r.facts.annotations.overrides.count("Foo::f"), 1u);
    EXPECT_EQ(r.facts.annotations.overrides.count("Foo::g"), 1u);
}

// ---- which statement ---------------------------------------------------------------------------------------------------

struct Span {
    unsigned begin_line, begin_col, end_line, end_col;
};

Span span_of(const Read& r, std::size_t i = 0) {
    const frontend::StatementRange& t = r.facts.annotations.shorts.at(i).target;
    return {t.begin.line, t.begin.column, t.end.line, t.end.column};
}

TEST(AnnotationTarget, TheNextStatementWhenTheCommentStandsAlone) {
    const Read r = read("void f() {\n    // @mt: returns int\n    foo.run(1);\n    other();\n}\n");
    ASSERT_EQ(r.facts.annotations.shorts.size(), 1u);
    const Span s = span_of(r);
    EXPECT_EQ(s.begin_line, 3u);
    EXPECT_EQ(s.begin_col, 5u);
    EXPECT_EQ(s.end_line, 3u);
    EXPECT_EQ(s.end_col, 16u) << "just past the ';'";
}

TEST(AnnotationTarget, TheStatementBeforeWhenTheCommentTrailsIt) {
    const Read r = read("void f() {\n    first();\n    foo.run(1);  // @mt: returns int\n    last();\n}\n");
    ASSERT_EQ(r.facts.annotations.shorts.size(), 1u);
    EXPECT_EQ(span_of(r).begin_line, 3u);
    EXPECT_EQ(span_of(r).end_line, 3u);
}

TEST(AnnotationTarget, TheLastStatementOfSeveralOnTheLine) {
    const Read r = read("void f() { a(); b();  // @mt: ignore\n c(); }\n");
    ASSERT_EQ(r.facts.annotations.shorts.size(), 1u);
    EXPECT_EQ(span_of(r).begin_col, 17u) << "the column of `b();`, not of `a();`";
}

TEST(AnnotationTarget, AMultiLineStatementIsOneTarget) {
    const Read r = read("void f() {\n    // @mt: returns int\n    EXPECT_EQ(foo.size(),\n              3u);\n}\n");
    EXPECT_EQ(span_of(r).begin_line, 3u);
    EXPECT_EQ(span_of(r).end_line, 4u);
}

TEST(AnnotationTarget, ACommentInsideAStatementIsAboutThatStatement) {
    const Read r = read("void f() {\n    EXPECT_EQ(foo.size(),  // @mt: returns unsigned\n              3u);\n    other();\n}\n");
    ASSERT_EQ(r.facts.annotations.shorts.size(), 1u);
    EXPECT_EQ(span_of(r).begin_line, 2u);
    EXPECT_EQ(span_of(r).end_line, 3u);
}

TEST(AnnotationTarget, SkipsOtherCommentsBetween) {
    const Read r = read("void f() {\n    // @mt: returns int\n    // an explanation\n    foo.run();\n}\n");
    EXPECT_EQ(span_of(r).begin_line, 4u);
}

TEST(AnnotationTarget, NothingAfterTheLastStatement) {
    const Read r = read("void f() {\n    foo.run();\n    // @mt: returns int\n}\n");
    ASSERT_EQ(r.facts.annotations.shorts.size(), 1u);
    EXPECT_FALSE(r.facts.annotations.shorts[0].target.valid());
}

TEST(AnnotationTarget, AtFileScopeItIsTheWholeDeclaration) {
    const Read r = read("// @mt: returns int\nvoid g() { foo.run(); }\n");
    ASSERT_EQ(r.facts.annotations.shorts.size(), 1u);
    EXPECT_EQ(span_of(r).begin_line, 2u);
}

// ---- applying the short directives ---------------------------------------------------------------------------------------------

const frontend::MethodCall& method_named(const Outcome& o, const char* name) {
    static const frontend::MethodCall none;
    for (const frontend::MethodCall& m : o.result.facts.method_calls) {
        if (m.method == name) return m;
    }
    ADD_FAILURE() << "no method call " << name;
    return none;
}

TEST(AnnotationApply, ReturnsOnTheOneCallOfTheStatement) {
    const Outcome o = body(
        "Foo foo;\n"
        "// @mt: returns std::vector<int>\n"
        "auto items = foo.items();\n"
        "foo.count();  // @mt: returns unsigned\n");
    EXPECT_TRUE(errors(o.diagnostics).empty());
    const frontend::MethodCall& items = method_named(o, "items");
    ASSERT_TRUE(items.annotation.returns.has_value());
    EXPECT_EQ(items.annotation.returns->spelling, "std::vector<int>");
    EXPECT_EQ(items.annotation.returns->origin, ir::Origin::Annotated);
    EXPECT_EQ(items.annotation.returns->where.line, 4u) << "the comment, for the TODO that cites it";
    EXPECT_EQ(items.use.kind, frontend::UseKind::AutoBound) << "the fact about the call is left as it was";
    ASSERT_TRUE(method_named(o, "count").annotation.returns.has_value());
    EXPECT_EQ(method_named(o, "count").annotation.returns->spelling, "unsigned");
}

TEST(AnnotationApply, ACallThatStartsTheStatementIsInsideIt) {
    // The location of a free call is its own first character, which is also the
    // first character of the statement.
    const Outcome o = body("// @mt: returns int\ncompute(3);\nother();  // @mt: ignore\n");
    EXPECT_TRUE(errors(o.diagnostics).empty()) << ::testing::PrintToString(messages(o.diagnostics));
    EXPECT_TRUE(o.result.facts.free_calls[0].annotation.returns.has_value());
    EXPECT_TRUE(o.result.facts.free_calls[1].annotation.ignore);
}

TEST(AnnotationApply, OverloadAndIgnore) {
    const Outcome o = body("Foo foo;\n// @mt: overload\nfoo.set(1);\nfoo.set(2.5);  // @mt: ignore\n");
    EXPECT_TRUE(errors(o.diagnostics).empty());
    EXPECT_TRUE(o.result.facts.method_calls[0].annotation.overload);
    EXPECT_FALSE(o.result.facts.method_calls[0].annotation.ignore);
    EXPECT_TRUE(o.result.facts.method_calls[1].annotation.ignore);
    EXPECT_FALSE(o.result.facts.method_calls[1].annotation.overload);
}

TEST(AnnotationApply, AnyKindOfCall) {
    const Outcome o = body(
        "Foo foo;\n"
        "// @mt: returns int\n"
        "int a = Foo::create();\n"
        "// @mt: returns long\n"
        "long b = compute(3);\n"
        "// @mt: returns double\n"
        "double c = foo.ratio;\n"
        "// @mt: returns char\n"
        "char d = util::first();\n");
    EXPECT_TRUE(errors(o.diagnostics).empty()) << ::testing::PrintToString(messages(o.diagnostics));
    EXPECT_TRUE(o.result.facts.scoped_calls[0].annotation.returns.has_value());   // Foo::create
    EXPECT_TRUE(o.result.facts.free_calls[0].annotation.returns.has_value());     // compute
    EXPECT_TRUE(o.result.facts.field_accesses[0].annotation.returns.has_value()); // foo.ratio
    EXPECT_TRUE(o.result.facts.scoped_calls[1].annotation.returns.has_value());   // util::first
}

TEST(AnnotationApply, CallsToSomebodyElsesCodeDoNotCount) {
    const Outcome o = body("Foo foo;\n// @mt: returns unsigned\nEXPECT_EQ(std::max(1, 2) + strlen(\"x\"), foo.size());\n");
    EXPECT_TRUE(errors(o.diagnostics).empty());
    EXPECT_TRUE(method_named(o, "size").annotation.returns.has_value());
}

TEST(AnnotationApply, ACallFoundByClassificationCounts) {
    const Outcome o = body("Foo other;\n// @mt: returns unsigned\nEXPECT_EQ(Foo(3).size(), 3u);\n");
    EXPECT_TRUE(errors(o.diagnostics).empty());
    EXPECT_TRUE(method_named(o, "size").annotation.returns.has_value());
}

TEST(AnnotationApply, ManyCallsInOneStatementIsAnErrorThatSaysWhich) {
    const Outcome o = body("Foo foo;\n// @mt: returns int\nEXPECT_EQ(foo.a(), foo.b());\n");
    EXPECT_EQ(errors(o.diagnostics), kOneError);
    EXPECT_TRUE(mentions(messages(o.diagnostics), "2 calls"));
    EXPECT_TRUE(mentions(messages(o.diagnostics), "Foo::a, Foo::b"));
    EXPECT_TRUE(mentions(messages(o.diagnostics), "Class::member returns TYPE"));
    EXPECT_FALSE(method_named(o, "a").annotation.returns.has_value()) << "applied to neither";
}

TEST(AnnotationApply, NoCallInTheStatementIsAnError) {
    const Outcome o = body("Foo foo;\n// @mt: returns int\nint x = 3;\nstd::sort(a, b);  // @mt: ignore\n");
    EXPECT_EQ(errors(o.diagnostics), (std::vector<std::string>{"mt-annotation", "mt-annotation"}));
    EXPECT_TRUE(mentions(messages(o.diagnostics), "no call to the code under test"));
}

TEST(AnnotationApply, NoStatementAtAllIsAnError) {
    const Outcome o = body("Foo foo;\nfoo.run();\n// @mt: returns int");
    EXPECT_EQ(errors(o.diagnostics), kOneError);
    EXPECT_TRUE(mentions(messages(o.diagnostics), "no statement after this annotation"));
}

TEST(AnnotationApply, TwoReturnsForTheSameCallAreAnError) {
    const Outcome o = body("Foo foo;\n// @mt: returns int\nfoo.count();  // @mt: returns long\n");
    EXPECT_EQ(errors(o.diagnostics), kOneError);
    EXPECT_TRUE(mentions(messages(o.diagnostics), "already has a 'returns'"));
    EXPECT_EQ(method_named(o, "count").annotation.returns->spelling, "int") << "the first stands";
}

TEST(AnnotationApply, ReturnsPlusOverloadFromTwoCommentsCombine) {
    const Outcome o = body("Foo foo;\n// @mt: overload\nfoo.set(1);  // @mt: returns int\n");
    EXPECT_TRUE(errors(o.diagnostics).empty());
    EXPECT_TRUE(method_named(o, "set").annotation.overload);
    EXPECT_TRUE(method_named(o, "set").annotation.returns.has_value());
}

TEST(AnnotationApply, WithNoAnnotationsNothingChanges) {
    const Outcome o = body("Foo foo;\nfoo.count();\n");
    EXPECT_FALSE(method_named(o, "count").annotation.returns.has_value());
    EXPECT_FALSE(method_named(o, "count").annotation.overload);
    EXPECT_FALSE(method_named(o, "count").annotation.ignore);
}

// ---- what reaches the targets --------------------------------------------------------------------------------------------------

TEST(AnnotationClasses, NamespaceAndHeaderLandOnTheClass) {
    const Outcome o = classify(
        "// @mt: class Foo namespace acme::core; class Foo header \"core/foo.h\"\n"
        "TEST(S, N) { Foo f; Bar b; }\n");
    ASSERT_EQ(o.result.classes.size(), 2u);
    EXPECT_EQ(o.result.classes[0].qualified(), "acme::core::Foo");
    EXPECT_EQ(o.result.classes[0].header, "core/foo.h");
    EXPECT_EQ(o.result.classes[1].qualified(), "Bar");
    EXPECT_TRUE(o.result.classes[1].header.empty());
}

TEST(AnnotationClasses, AnAnnotationForAClassNobodyUsesIsHarmless) {
    const Outcome o = classify("// @mt: class Ghost namespace x\nTEST(S, N) { Foo f; }\n");
    EXPECT_TRUE(errors(o.diagnostics).empty());
    ASSERT_EQ(o.result.classes.size(), 1u);
    EXPECT_EQ(o.result.classes[0].qualified(), "Foo");
}

TEST(AnnotationOverrides, TheFileLaysItsAnnotationsOverTheConfiguration) {
    frontend::ClassifyOptions options;
    {
        core::Diagnostics d;
        ASSERT_TRUE(core::apply_config_json(
            options.config,
            R"({"overrides": {"acme::Foo::f": {"returns": "long", "const": true, "params": {"1": "int", "2": "int"}},
                              "acme::Foo::g": {"static": true}}})",
            "cfg.json", d))
            << "the configuration in the test is valid";
    }
    const Outcome o = classify(
        "// @mt: acme::Foo::f returns std::string; acme::Foo::f param 2 double; acme::Foo::h noexcept\n"
        "TEST(S, N) { acme::Foo foo; foo.f(1, 2); }\n", options);
    EXPECT_TRUE(errors(o.diagnostics).empty());

    const auto& merged = o.result.overrides;
    ASSERT_EQ(merged.size(), 3u);
    const core::MemberOverride& f = merged.at("acme::Foo::f");
    EXPECT_EQ(f.returns, std::optional<std::string>("std::string")) << "the annotation wins";
    EXPECT_EQ(f.is_const, std::optional<bool>(true)) << "what it does not mention stays";
    EXPECT_EQ(f.params.at(1), "int");
    EXPECT_EQ(f.params.at(2), "double");
    EXPECT_EQ(merged.at("acme::Foo::g").is_static, std::optional<bool>(true));
    EXPECT_EQ(merged.at("acme::Foo::h").is_noexcept, std::optional<bool>(true));
}

TEST(AnnotationOverrides, NoneAtAllIsEmpty) {
    const Outcome o = body("Foo foo;\nfoo.run();\n");
    EXPECT_TRUE(o.result.overrides.empty());
}

TEST(AnnotationRobustness, EveryPrefixOfAnAnnotatedFileIsReadWithoutIncident) {
    const std::string text =
        "// @mt: class Foo namespace acme; class Foo header \"acme/foo.h\"\n"
        "// @mt: acme::Foo::f returns std::vector<int>; acme::Foo::f param 1 long; acme::Foo::g const\n"
        "TEST(S, N) {\n"
        "    Foo foo;\n"
        "    // @mt: returns unsigned\n"
        "    EXPECT_EQ(foo.size(), 3u);\n"
        "    foo.run();  // @mt: ignore; overload\n"
        "    EXPECT_EQ(foo.a(),  // @mt: returns int\n"
        "              1);\n"
        "}\n";
    for (std::size_t n = 0; n <= text.size(); ++n) {
        const Outcome o = classify(text.substr(0, n));
        EXPECT_LE(o.result.classes.size(), 4u) << "cut at " << n;
    }
}

}  // namespace
