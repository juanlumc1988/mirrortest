// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The whole tests frontend, file in and model out: what an argument that is the
// result of another call learns, and chains resolved a link per round.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/frontend/pipeline.h"

using namespace mt;

namespace {

struct Outcome {
    frontend::Analysis analysis;
    core::Diagnostics  diagnostics;
    const ir::Model&   model() const { return analysis.model; }
};

Outcome analyse(const std::string& text, core::Config config = {}) {
    Outcome o;
    frontend::AnalysisOptions options;
    options.config = std::move(config);
    o.analysis = frontend::analyse({"t.cpp", text}, options, o.diagnostics);
    return o;
}

Outcome body(const std::string& statements, core::Config config = {}) {
    return analyse("#include <gtest/gtest.h>\nTEST(S, N) {\n" + statements + "\n}\n", std::move(config));
}

const ir::Class* find_class(const Outcome& o, const char* name) {
    for (const ir::Class& c : o.model().classes) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

const ir::Method* find_method(const Outcome& o, const char* cls, const char* name, std::size_t arity = 0) {
    const ir::Class* c = find_class(o, cls);
    if (c == nullptr) return nullptr;
    for (const ir::Method& m : c->methods) {
        if (m.name == name && m.params.size() == arity && m.kind != ir::MethodKind::Constructor) return &m;
    }
    return nullptr;
}

std::string ret_of(const Outcome& o, const char* cls, const char* name, std::size_t arity = 0) {
    const ir::Method* m = find_method(o, cls, name, arity);
    EXPECT_NE(m, nullptr) << cls << "::" << name;
    return m != nullptr ? m->ret.spelling : "<missing>";
}

std::vector<std::string> param_types(const Outcome& o, const char* cls, const char* name, std::size_t arity) {
    std::vector<std::string> out;
    const ir::Method* m = find_method(o, cls, name, arity);
    EXPECT_NE(m, nullptr) << cls << "::" << name;
    if (m != nullptr) {
        for (const ir::Param& p : m->params) out.push_back(p.type.spelling);
    }
    return out;
}

std::vector<std::string> codes(const Outcome& o, core::Severity severity) {
    std::vector<std::string> out;
    for (const core::Diagnostic& d : o.diagnostics.sorted()) {
        if (d.severity == severity) out.push_back(d.code);
    }
    return out;
}
std::vector<std::string> errors(const Outcome& o) { return codes(o, core::Severity::Error); }
std::vector<std::string> warnings(const Outcome& o) { return codes(o, core::Severity::Warning); }

bool has_note(const ir::Method* m, const std::string& text) {
    if (m == nullptr) return false;
    for (const std::string& n : m->notes) {
        if (n.find(text) != std::string::npos) return true;
    }
    return false;
}

std::vector<std::string> class_names(const Outcome& o) {
    std::vector<std::string> out;
    for (const ir::Class& c : o.model().classes) out.push_back(c.name);
    return out;
}

// ---- an argument that is the result of another call ------------------------------------------------------------------

TEST(PipelineArguments, TakeTheTypeTheOtherCallWasInferredToReturn) {
    const Outcome o = body("Foo foo;\nBar bar;\nEXPECT_EQ(foo.size(), 3u);\nbar.set(foo.size());\n");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    EXPECT_EQ(param_types(o, "Bar", "set", 1), std::vector<std::string>{"unsigned int"});
    EXPECT_TRUE(has_note(find_method(o, "Bar", "set", 1), "parameter 1 of 'Bar::set' inferred from the result of 'Foo::size'"));
}

TEST(PipelineArguments, TheOrderOfTheStatementsDoesNotMatter) {
    const Outcome o = body("Foo foo;\nBar bar;\nbar.set(foo.size());\nEXPECT_EQ(foo.size(), 3u);\n");
    EXPECT_EQ(param_types(o, "Bar", "set", 1), std::vector<std::string>{"unsigned int"});
    EXPECT_TRUE(errors(o).empty());
}

TEST(PipelineArguments, ClassesComeByConstReferenceAndValuesByValue) {
    const Outcome o = body(
        "Foo foo;\nBar bar;\n"
        "Baz b = foo.make();\nstd::size_t n = foo.count();\n"
        "bar.use(foo.make(), foo.count());\n");
    EXPECT_EQ(param_types(o, "Bar", "use", 2), (std::vector<std::string>{"const Baz&", "std::size_t"}));
}

TEST(PipelineArguments, ThroughSeveralLevels) {
    const Outcome o = body("A a;\nB b;\nC c;\nEXPECT_EQ(a.get(), 'x');\nb.put(a.get());\nc.take(b.put(a.get()));\nEXPECT_EQ(b.put(a.get()), 2.5);\n");
    EXPECT_EQ(param_types(o, "B", "put", 1), std::vector<std::string>{"char"});
    EXPECT_EQ(param_types(o, "C", "take", 1), std::vector<std::string>{"double"});
}

TEST(PipelineArguments, ThroughAFreeFunctionAndAStaticCall) {
    const Outcome o = body("Bar bar;\nEXPECT_EQ(twice(2), 4);\nEXPECT_EQ(Foo::version(), 2u);\nbar.f(twice(2), Foo::version());\n");
    EXPECT_EQ(param_types(o, "Bar", "f", 2), (std::vector<std::string>{"int", "unsigned int"}));
}

TEST(PipelineArguments, AnAnnotatedResultIsKnownToo) {
    const Outcome o = body("Foo foo;\nBar bar;\n// @mt: returns std::vector<int>\nauto v = foo.items();\nbar.take(foo.items());\n");
    EXPECT_EQ(param_types(o, "Bar", "take", 1), std::vector<std::string>{"const std::vector<int>&"});
}

TEST(PipelineArguments, AResultNobodyTypedStaysUnknown) {
    const Outcome o = body("Foo foo;\nBar bar;\nbar.set(foo.size());\n");
    EXPECT_EQ(errors(o), (std::vector<std::string>{"mt-unknown-param", "mt-unknown-return"}));
}

TEST(PipelineArguments, APlaceholderIsNotPropagated) {
    // With auto_policy todo foo.size() is *assumed* int; that guess must not
    // become the declared-looking type of set's parameter.
    core::Config c;
    c.auto_policy = core::AutoPolicy::Todo;
    const Outcome o = body("Foo foo;\nBar bar;\nbar.set(foo.size());\n", c);
    EXPECT_TRUE(errors(o).empty());
    const ir::Method* set = find_method(o, "Bar", "set", 1);
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->params[0].type.origin, ir::Origin::Defaulted) << "its own placeholder, not inherited from size()";
}

TEST(PipelineArguments, AFunctionalCastGivesItsClass) {
    const Outcome o = body("Foo other;\nBar bar;\nbar.use(Foo(3), QString(\"x\"), size_t(2));\n");
    EXPECT_EQ(param_types(o, "Bar", "use", 3), (std::vector<std::string>{"const Foo&", "const QString&", "size_t"}));
    EXPECT_TRUE(errors(o).empty());
}

TEST(PipelineArguments, ACastToAClassInANamespaceIsQualified) {
    const Outcome o = body("acme::Foo other;\nBar bar;\nbar.use(Foo(3));\n");
    EXPECT_EQ(param_types(o, "Bar", "use", 1), std::vector<std::string>{"const acme::Foo&"});
}

// ---- chained calls, a link per round --------------------------------------------------------------------------------------

TEST(PipelineChains, AnAnnotationOnTheRootMakesTheNextCallAMethodOfItsType) {
    const Outcome o = body("Repo repo;\n// @mt: returns Item\nEXPECT_EQ(repo.find(1).name(), \"x\");\n");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    EXPECT_EQ(class_names(o), (std::vector<std::string>{"Repo", "Item"}));
    EXPECT_EQ(ret_of(o, "Repo", "find", 1), "Item");
    EXPECT_EQ(ret_of(o, "Item", "name"), "std::string");
}

TEST(PipelineChains, ADeclaredUseOfTheRootIsEnough) {
    const Outcome o = body("Repo repo;\nItem item = repo.find(2);\nEXPECT_TRUE(repo.find(1).valid());\n");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    EXPECT_EQ(ret_of(o, "Repo", "find", 1), "Item");
    EXPECT_EQ(ret_of(o, "Item", "valid"), "bool");
}

TEST(PipelineChains, SeveralLinks) {
    const Outcome o = analyse(
        "// @mt: Repo::find returns Item; Item::owner returns Person\n"
        "TEST(S, N) { Repo repo; EXPECT_EQ(repo.find(1).owner().name(), \"x\"); }\n");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    EXPECT_EQ(class_names(o), (std::vector<std::string>{"Repo", "Item", "Person"}));
    EXPECT_EQ(ret_of(o, "Item", "owner"), "Person");
    EXPECT_EQ(ret_of(o, "Person", "name"), "std::string");
}

TEST(PipelineChains, ThroughAnArrowAndAConstReference) {
    const Outcome o = analyse(
        "// @mt: Repo::find returns const Item*; Repo::peek returns const Item&\n"
        "TEST(S, N) { Repo repo; EXPECT_EQ(repo.find(1)->name(), \"x\"); EXPECT_EQ(repo.peek().size(), 1u); }\n");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    ASSERT_NE(find_method(o, "Item", "name"), nullptr);
    EXPECT_TRUE(find_method(o, "Item", "name")->is_const) << "through a pointer to const";
    EXPECT_TRUE(find_method(o, "Item", "size")->is_const) << "through a const reference";
}

TEST(PipelineChains, ThroughASmartPointer) {
    const Outcome o = analyse(
        "// @mt: Repo::find returns std::unique_ptr<Item>\n"
        "TEST(S, N) { Repo repo; EXPECT_EQ(repo.find(1)->name(), \"x\"); }\n");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    EXPECT_EQ(ret_of(o, "Item", "name"), "std::string");
}

TEST(PipelineChains, AnAutoVariableOfAResolvedChain) {
    const Outcome o = analyse(
        "// @mt: Repo::find returns Item\n"
        "TEST(S, N) { Repo repo; auto item = repo.find(1); EXPECT_EQ(item.size(), 3u); }\n");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    EXPECT_EQ(ret_of(o, "Item", "size"), "unsigned int");
}

TEST(PipelineChains, TheResultOfAResolvedLinkCanBeAnArgument) {
    const Outcome o = analyse(
        "// @mt: Repo::find returns Item\n"
        "TEST(S, N) { Repo repo; Bar bar; EXPECT_EQ(repo.find(1).count(), 3u); bar.use(repo.find(1).count()); }\n");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    EXPECT_EQ(param_types(o, "Bar", "use", 1), std::vector<std::string>{"unsigned int"});
}

TEST(PipelineChains, AnExternalTypeHasNothingToGenerate) {
    const Outcome o = analyse(
        "// @mt: Repo::names returns std::vector<std::string>\n"
        "TEST(S, N) { Repo repo; EXPECT_EQ(repo.names().size(), 3u); }\n");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    EXPECT_EQ(class_names(o), std::vector<std::string>{"Repo"});
    EXPECT_EQ(ret_of(o, "Repo", "names"), "std::vector<std::string>");
}

TEST(PipelineChains, ACallOnAValueThatHasNoMembersIsAnError) {
    const Outcome o = analyse(
        "// @mt: Repo::count returns int\n"
        "TEST(S, N) { Repo repo; EXPECT_EQ(repo.count().size(), 3u); }\n");
    EXPECT_EQ(errors(o), std::vector<std::string>{"mt-chained-call"});
}

TEST(PipelineChains, WithNothingToGoOnTheChainIsTheErrorItWas) {
    const Outcome o = body("Repo repo;\nEXPECT_EQ(repo.find(1).name(), \"x\");\n");
    EXPECT_EQ(errors(o), std::vector<std::string>{"mt-chained-call"});
    EXPECT_EQ(class_names(o), std::vector<std::string>{"Repo"});
}

TEST(PipelineChains, AFunctionalCastRootIsSettledByClassification) {
    const Outcome o = body("Foo other;\nEXPECT_EQ(Foo(3).size(), 3u);\n// @mt: returns Foo\nEXPECT_EQ(Foo(3).twice().size(), 6u);\n");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    EXPECT_EQ(ret_of(o, "Foo", "size"), "unsigned int");
    EXPECT_EQ(ret_of(o, "Foo", "twice"), "Foo") << "the annotation on the middle link";
}

TEST(PipelineChains, ARootThatIsAFreeFunction) {
    const Outcome o = analyse(
        "// @mt: make returns Item\n"
        "TEST(S, N) { EXPECT_EQ(make().name(), \"x\"); }\n");
    // `make returns` is not a member name, so the long form is rejected; the short
    // form on the statement is the way.
    EXPECT_FALSE(errors(o).empty());

    const Outcome ok = body("// @mt: returns Item\nEXPECT_EQ(make().name(), \"x\");\n");
    EXPECT_TRUE(errors(ok).empty()) << ::testing::PrintToString(errors(ok));
    EXPECT_EQ(ret_of(ok, "Item", "name"), "std::string");
}

TEST(PipelineChains, ARootThatIsAStaticCall) {
    const Outcome o = body("// @mt: returns Item\nEXPECT_EQ(Repo::instance().name(), \"x\");\n");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    EXPECT_EQ(class_names(o), (std::vector<std::string>{"Repo", "Item"}));
}

TEST(PipelineChains, ARootThatIsAField) {
    const Outcome o = body("Repo repo;\nItem item = repo.current;\nEXPECT_EQ(repo.current.name(), \"x\");\n");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    EXPECT_EQ(ret_of(o, "Item", "name"), "std::string");
}

TEST(PipelineChains, ALongChainStopsAtTheCapWithAWarning) {
    // Five links, all typed: one round per link, and the cap is four.
    const Outcome o = analyse(
        "// @mt: A::a returns B; B::b returns C; C::c returns D; D::d returns E; E::e returns F\n"
        "TEST(S, N) { A x; EXPECT_EQ(x.a().b().c().d().e().f(), 1); }\n");
    EXPECT_FALSE(warnings(o).empty());
    bool found = false;
    for (const core::Diagnostic& d : o.diagnostics.sorted()) {
        if (d.message.find("a chain of more than 4 links") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

// ---- the whole --------------------------------------------------------------------------------------------------------------

TEST(PipelineWhole, DiagnosticsOfTheFinalRoundAreReportedOnceEach) {
    const Outcome o = analyse(
        "// @mt: Repo::find returns Item\n"
        "TEST(S, N) { Repo repo; EXPECT_EQ(repo.find(1).name(), \"x\"); repo.run(); repo.run(); }\n");
    std::size_t unused = 0;
    for (const core::Diagnostic& d : o.diagnostics.sorted()) {
        if (d.code == "mt-unused-result") ++unused;
    }
    EXPECT_EQ(unused, 1u) << "once, not once per round";
}

TEST(PipelineWhole, FrontendDiagnosticsAreIncludedOnce) {
    const Outcome o = analyse("TEST(S, N) { Foo f(); mystery.run(); }\n");
    std::size_t vexing = 0, receiver = 0;
    for (const core::Diagnostic& d : o.diagnostics.sorted()) {
        if (d.code == "mt-vexing-parse") ++vexing;
        if (d.code == "mt-unknown-receiver") ++receiver;
    }
    EXPECT_EQ(vexing, 1u);
    EXPECT_EQ(receiver, 1u);
}

TEST(PipelineWhole, TheSameInputGivesTheSameModel) {
    const std::string text =
        "// @mt: Repo::find returns Item; Item::owner returns Person\n"
        "TEST(S, N) { Repo repo; Bar bar; EXPECT_EQ(repo.find(1).owner().name(), \"x\"); bar.set(repo.find(2).owner().age()); }\n";
    const Outcome a = analyse(text);
    const Outcome b = analyse(text);
    EXPECT_EQ(class_names(a), class_names(b));
    EXPECT_EQ(a.diagnostics.sorted(), b.diagnostics.sorted());
    ASSERT_EQ(a.model().classes.size(), b.model().classes.size());
    for (std::size_t i = 0; i < a.model().classes.size(); ++i) {
        EXPECT_EQ(a.model().classes[i].methods.size(), b.model().classes[i].methods.size());
    }
}

TEST(PipelineWhole, EveryPrefixOfAFileWithChainsIsAnalysedWithoutIncident) {
    const std::string text =
        "// @mt: Repo::find returns Item; Item::owner returns Person\n"
        "TEST(S, N) {\n"
        "    Repo repo;\n"
        "    Bar bar;\n"
        "    EXPECT_EQ(repo.find(1).owner().name(), \"x\");\n"
        "    auto item = repo.find(2);\n"
        "    bar.set(item.size(), Foo(3));\n"
        "    EXPECT_EQ(item.size(), 3u);\n"
        "}\n";
    for (std::size_t n = 0; n <= text.size(); ++n) {
        const Outcome o = analyse(text.substr(0, n));
        EXPECT_LE(o.model().classes.size(), 6u) << "cut at " << n;
    }
}

}  // namespace
