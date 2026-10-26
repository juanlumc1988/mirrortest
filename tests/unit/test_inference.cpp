// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// "Facts in, model out" -- written as C++ fragments in, model out, so that the
// facts are the real ones the frontend produces.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/frontend/classify.h"
#include "mirrortest/frontend/code_frontend.h"
#include "mirrortest/frontend/inference.h"
#include "mirrortest/frontend/test_frontend.h"

using namespace mt;

namespace {

struct Outcome {
    ir::Model         model;
    core::Diagnostics diagnostics;   // every stage's
};

Outcome infer_text(const std::string& text, core::Config config = {}, std::vector<ir::Model> known = {}) {
    Outcome o;
    frontend::TestFrontendOptions fo;
    fo.standard = config.cxx_std;
    frontend::TestFileFacts facts = frontend::read_test_file({"t.cpp", text}, fo, o.diagnostics);
    frontend::ClassifyOptions co;
    co.config = config;
    co.known = known;
    const frontend::Classified classified = frontend::classify(std::move(facts), co, o.diagnostics);
    o.model = frontend::infer(classified, config, known, o.diagnostics);
    return o;
}

Outcome body(const std::string& statements, core::Config config = {}) {
    return infer_text("#include <gtest/gtest.h>\nTEST(S, N) {\n" + statements + "\n}\n", std::move(config));
}

const ir::Class& the_class(const Outcome& o, const char* name) {
    static const ir::Class none;
    for (const ir::Class& c : o.model.classes) {
        if (c.name == name) return c;
    }
    ADD_FAILURE() << "no class " << name;
    return none;
}

const ir::Method* find_method(const ir::Class& c, const char* name, std::size_t arity) {
    for (const ir::Method& m : c.methods) {
        if (m.name == name && m.params.size() == arity && m.kind != ir::MethodKind::Constructor) return &m;
    }
    return nullptr;
}

const ir::Method& method(const Outcome& o, const char* cls, const char* name, std::size_t arity = 0) {
    static const ir::Method none;
    const ir::Method* m = find_method(the_class(o, cls), name, arity);
    EXPECT_NE(m, nullptr) << cls << "::" << name << "/" << arity;
    return m != nullptr ? *m : none;
}

std::vector<const ir::Method*> constructors(const ir::Class& c) {
    std::vector<const ir::Method*> out;
    for (const ir::Method& m : c.methods) {
        if (m.kind == ir::MethodKind::Constructor) out.push_back(&m);
    }
    return out;
}

std::vector<std::string> param_types(const ir::Method& m) {
    std::vector<std::string> out;
    for (const ir::Param& p : m.params) out.push_back(p.type.spelling);
    return out;
}

std::vector<std::string> param_names(const ir::Method& m) {
    std::vector<std::string> out;
    for (const ir::Param& p : m.params) out.push_back(p.name);
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

bool has_note(const std::vector<std::string>& notes, const std::string& text) {
    for (const std::string& n : notes) {
        if (n.find(text) != std::string::npos) return true;
    }
    return false;
}

core::Config todo_policy() {
    core::Config c;
    c.auto_policy = core::AutoPolicy::Todo;
    return c;
}

// ---- the example of the design ------------------------------------------------------------------------------------------

TEST(InferenceBasics, TheExampleOfTheDesign) {
    const Outcome o = infer_text(
        "#include <gtest/gtest.h>\n"
        "TEST(FooTest, StartsWithThreeItems) {\n"
        "    const acme::Foo foo(3);\n"
        "    EXPECT_EQ(foo.size(), 3u);\n"
        "    EXPECT_FALSE(foo.empty());\n"
        "}\n");
    EXPECT_TRUE(o.diagnostics.empty()) << ::testing::PrintToString(warnings(o));
    ASSERT_EQ(o.model.classes.size(), 1u);
    const ir::Class& foo = o.model.classes[0];
    EXPECT_EQ(foo.name, "Foo");
    EXPECT_EQ(foo.ns, std::vector<std::string>{"acme"});

    const auto ctors = constructors(foo);
    ASSERT_EQ(ctors.size(), 1u);
    EXPECT_TRUE(ctors[0]->is_explicit);
    EXPECT_EQ(param_types(*ctors[0]), std::vector<std::string>{"int"});
    EXPECT_EQ(param_names(*ctors[0]), std::vector<std::string>{"arg1"});

    const ir::Method& size = method(o, "Foo", "size");
    EXPECT_TRUE(size.is_const) << "called on a const object";
    EXPECT_EQ(size.ret.spelling, "unsigned int");
    EXPECT_EQ(size.ret.origin, ir::Origin::Inferred);
    ASSERT_EQ(size.notes.size(), 1u);
    EXPECT_EQ(size.notes[0], "return type inferred from literal 3u (t.cpp:4)");

    const ir::Method& empty = method(o, "Foo", "empty");
    EXPECT_TRUE(empty.is_const);
    EXPECT_EQ(empty.ret.spelling, "bool");
}

TEST(InferenceBasics, ConstructorsComeFirstThenMethodsInOrderOfAppearance) {
    const Outcome o = body("Foo foo(1);\nfoo.zed();\nfoo.alpha();\nfoo.mid();\n");
    std::vector<std::string> names;
    for (const ir::Method& m : the_class(o, "Foo").methods) names.push_back(m.name);
    EXPECT_EQ(names, (std::vector<std::string>{"Foo", "zed", "alpha", "mid"}));
}

TEST(InferenceBasics, AClassWithOnlyFieldsIsAStruct) {
    const Outcome o = body("Point p;\np.x = 1;\np.y = 2;\nOther q;\nq.run();\n");
    EXPECT_TRUE(the_class(o, "Point").is_struct);
    EXPECT_FALSE(the_class(o, "Other").is_struct);
}

TEST(InferenceBasics, ATypeOnlyUsedAsATypeIsAnEmptyClass) {
    const Outcome o = body("std::vector<Item> items;\n");
    ASSERT_EQ(o.model.classes.size(), 1u);
    EXPECT_TRUE(o.model.classes[0].methods.empty());
    EXPECT_TRUE(o.model.classes[0].fields.empty());
    EXPECT_FALSE(o.model.classes[0].is_struct);
}

TEST(InferenceBasics, TheAnnotatedHeaderTravelsWithTheClass) {
    const Outcome o = infer_text("// @mt: class Foo header \"acme/foo.h\"\nTEST(S, N) { Foo f; }\n");
    EXPECT_EQ(the_class(o, "Foo").header, "acme/foo.h");
}

TEST(InferenceBasics, ClassTemplatesAreReportedNotGenerated) {
    const Outcome o = body("Box<int> b(1);\nEXPECT_EQ(b.get(), 1);\n");
    EXPECT_EQ(warnings(o), std::vector<std::string>{"mt-unsupported"});
    EXPECT_EQ(the_class(o, "Box").name, "Box");
}

// ---- return types (7.6) -----------------------------------------------------------------------------------------------------

struct ReturnCase {
    const char* statements;     // uses `foo`, a Foo, and calls foo.m()
    const char* expected;
};

TEST(InferenceReturn, FromTheContextOfTheUse) {
    const ReturnCase cases[] = {
        {"EXPECT_EQ(foo.m(), 3);", "int"},
        {"EXPECT_EQ(foo.m(), 3u);", "unsigned int"},
        {"EXPECT_NE(foo.m(), 2.5f);", "float"},
        {"EXPECT_LT(foo.m(), 2.5);", "double"},
        {"EXPECT_EQ(foo.m(), 'c');", "char"},
        {"EXPECT_EQ(foo.m(), -3);", "int"},
        {"EXPECT_EQ(foo.m(), true);", "bool"},
        {"EXPECT_EQ(foo.m(), \"abc\");", "std::string"},
        {"EXPECT_EQ(\"abc\", foo.m());", "std::string"},
        {"EXPECT_TRUE(foo.m());", "bool"},
        {"ASSERT_FALSE(foo.m());", "bool"},
        {"EXPECT_STREQ(foo.m(), \"abc\");", "const char*"},
        {"EXPECT_FLOAT_EQ(foo.m(), 1.0f);", "float"},
        {"EXPECT_DOUBLE_EQ(foo.m(), 1.0);", "double"},
        {"EXPECT_NEAR(foo.m(), 1.0, 0.1);", "double"},
        {"if (foo.m()) {}", "bool"},
        {"bool b = !foo.m();", "bool"},
        {"bool b = foo.m() && true;", "bool"},
        {"EXPECT_TRUE(foo.m() > 3u);", "unsigned int"},
        {"int n = foo.m();", "int"},
        {"unsigned n = foo.m();", "unsigned"},
        {"std::size_t n = foo.m();", "std::size_t"},
        {"const std::string s = foo.m();", "std::string"},
        {"const std::string& s = foo.m();", "std::string"},
        {"std::string& s = foo.m();", "std::string&"},
        {"int n = 0;\nn = foo.m();", "int"},
        {"long n = 0;\nEXPECT_EQ(foo.m(), n);", "long"},
    };
    for (const ReturnCase& c : cases) {
        const Outcome o = body(std::string("Foo foo;\n") + c.statements + "\n");
        EXPECT_EQ(method(o, "Foo", "m").ret.spelling, c.expected) << c.statements;
        EXPECT_TRUE(errors(o).empty()) << c.statements;
    }
}

TEST(InferenceReturn, WhereTheTypeCameFromDecidesTheNote) {
    const Outcome inferred = body("Foo foo;\nEXPECT_EQ(foo.m(), 3);\n");
    EXPECT_TRUE(has_note(method(inferred, "Foo", "m").notes, "return type inferred from literal 3 (t.cpp:4)"));

    const Outcome declared = body("Foo foo;\nint n = foo.m();\n");
    EXPECT_EQ(method(declared, "Foo", "m").ret.origin, ir::Origin::Declared);
    EXPECT_TRUE(method(declared, "Foo", "m").notes.empty()) << "the user wrote this type";
}

TEST(InferenceReturn, ADiscardedResultIsVoidWithAWarning) {
    const Outcome o = body("Foo foo;\nfoo.run();\nfoo.run();\n");
    const ir::Method& run = method(o, "Foo", "run");
    EXPECT_TRUE(run.ret.is_void());
    EXPECT_EQ(run.ret.origin, ir::Origin::Defaulted);
    EXPECT_TRUE(has_note(run.notes, "result never used; assumed void (t.cpp:4)"));
    EXPECT_EQ(warnings(o), std::vector<std::string>{"mt-unused-result"}) << "once per method";
}

TEST(InferenceReturn, OneRealUseBeatsDiscardedOnes) {
    const Outcome o = body("Foo foo;\nfoo.m();\nEXPECT_EQ(foo.m(), 3);\n");
    EXPECT_EQ(method(o, "Foo", "m").ret.spelling, "int");
    EXPECT_TRUE(warnings(o).empty());
}

TEST(InferenceReturn, TheHigherOriginWinsAndTheDisagreementIsReported) {
    // 3 gives int (Inferred); the declared `long` is the user's own word.
    const Outcome o = body("Foo foo;\nEXPECT_EQ(foo.m(), 3);\nlong n = foo.m();\n");
    EXPECT_EQ(method(o, "Foo", "m").ret.spelling, "long");
    EXPECT_EQ(method(o, "Foo", "m").ret.origin, ir::Origin::Declared);
    EXPECT_EQ(warnings(o), std::vector<std::string>{"mt-conflict"});
}

TEST(InferenceReturn, EqualOriginsTheFirstWins) {
    const Outcome o = body("Foo foo;\nEXPECT_EQ(foo.m(), 3u);\nEXPECT_EQ(foo.m(), 3);\n");
    EXPECT_EQ(method(o, "Foo", "m").ret.spelling, "unsigned int");
    EXPECT_EQ(warnings(o), std::vector<std::string>{"mt-conflict"});
}

TEST(InferenceReturn, DifferentSpellingsOfOneTypeAreNotAConflict) {
    const Outcome o = body("Foo foo;\nunsigned n = 0;\nEXPECT_EQ(foo.m(), 3u);\nEXPECT_EQ(foo.m(), n);\n"
                           "std::size_t s = 0;\nsize_t t = 0;\nEXPECT_EQ(foo.k(), s);\nEXPECT_EQ(foo.k(), t);\n");
    EXPECT_TRUE(warnings(o).empty()) << ::testing::PrintToString(warnings(o));
}

TEST(InferenceReturn, WithNothingToGoOnIsAnErrorByDefault) {
    const Outcome o = body("Foo foo;\nbar(foo.m());\n");
    EXPECT_EQ(errors(o), (std::vector<std::string>{"mt-unknown-param", "mt-unknown-return"})) << "bar's parameter, and m's result";
}

TEST(InferenceReturn, AutoIsTheAutoError) {
    const Outcome o = body("Foo foo;\nauto n = foo.m();\n");
    EXPECT_EQ(errors(o), std::vector<std::string>{"mt-auto"});
}

TEST(InferenceReturn, AutoThatIsLaterComparedIsFine) {
    const Outcome o = body("Foo foo;\nauto n = foo.m();\nEXPECT_EQ(n, 5u);\n");
    EXPECT_TRUE(errors(o).empty());
    EXPECT_EQ(method(o, "Foo", "m").ret.spelling, "unsigned int");
    EXPECT_TRUE(has_note(method(o, "Foo", "m").notes, "through variable 'n'"));
}

TEST(InferenceReturn, ACallOnTheResultIsTheChainedCallError) {
    const Outcome o = body("Foo foo;\nfoo.make().run();\n");
    EXPECT_EQ(errors(o), std::vector<std::string>{"mt-chained-call"});
}

TEST(InferenceReturn, ThePolicyTodoDowngradesToAWarningAndAPlaceholder) {
    const Outcome o = body("Foo foo;\nbar(foo.a());\nauto n = foo.b();\nfoo.make().run();\n", todo_policy());
    EXPECT_TRUE(errors(o).empty());
    EXPECT_EQ(warnings(o), (std::vector<std::string>{"mt-unused-result", "mt-unknown-param", "mt-unknown-return", "mt-auto",
                                                      "mt-chained-call"}));
    for (const char* name : {"a", "b", "make"}) {
        const ir::Method& m = method(o, "Foo", name);
        EXPECT_EQ(m.ret.spelling, "int") << name;
        EXPECT_EQ(m.ret.origin, ir::Origin::Defaulted) << name;
        EXPECT_TRUE(has_note(m.notes, "assumed 'int'")) << name;
    }
}

TEST(InferenceReturn, ThePlaceholderTypeIsConfigurable) {
    core::Config c = todo_policy();
    c.placeholder_type = "std::uint32_t";
    const Outcome o = body("Foo foo;\nbar(foo.a());\n", c);
    EXPECT_EQ(method(o, "Foo", "a").ret.spelling, "std::uint32_t");
}

TEST(InferenceReturn, LiteralWarningsAreReportedWhenTheTypeIsUsed) {
    const Outcome o = body("Foo foo;\nEXPECT_EQ(foo.big(), 5000000000);\n");
    EXPECT_EQ(method(o, "Foo", "big").ret.spelling, "long long");
    EXPECT_EQ(warnings(o), std::vector<std::string>{"mt-literal-range"});
}

TEST(InferenceReturn, LiteralWarningsOfATypeNobodyUsesStaySilent) {
    // The declared type wins; the out-of-range literal never became a type.
    const Outcome o = body("Foo foo;\nstd::int64_t n = 0;\nEXPECT_EQ(foo.big(), 5000000000);\nEXPECT_EQ(foo.big(), n);\n");
    EXPECT_EQ(method(o, "Foo", "big").ret.spelling, "std::int64_t");
}

// ---- parameters -----------------------------------------------------------------------------------------------------------------

TEST(InferenceParams, LiteralsGiveTheirTypeByValue) {
    const Outcome o = body("Foo foo;\nfoo.set(1, 2u, 3.5, 4.5f, 'c', true, 5000000000);\nEXPECT_EQ(foo.set(1, 2u, 3.5, 4.5f, 'c', true, 5000000000), 0);\n");
    EXPECT_EQ(param_types(method(o, "Foo", "set", 7)),
              (std::vector<std::string>{"int", "unsigned int", "double", "float", "char", "bool", "long long"}));
}

TEST(InferenceParams, AStringLiteralIsAConstCharPointerUnlessConfigured) {
    const Outcome by_default = body("Foo foo;\nEXPECT_EQ(foo.name(\"abc\"), 1);\n");
    EXPECT_EQ(param_types(method(by_default, "Foo", "name", 1)), std::vector<std::string>{"const char*"});

    core::Config c;
    c.string_literal_param = "const std::string&";
    const Outcome configured = body("Foo foo;\nEXPECT_EQ(foo.name(\"abc\"), 1);\n", c);
    EXPECT_EQ(param_types(method(configured, "Foo", "name", 1)), std::vector<std::string>{"const std::string&"});
}

TEST(InferenceParams, VariablesGiveByValueOrConstReference) {
    const Outcome o = body(
        "Foo foo;\n"
        "int n = 0;\nconst long m = 0;\nunsigned& r = n;\ndouble* p = nullptr;\n"
        "std::string s;\nBar bar;\nstd::vector<int> v;\nstd::size_t z = 0;\nstd::uint8_t b = 0;\n"
        "std::string_view sv;\nstd::chrono::seconds t;\nMode mode;\n"
        "EXPECT_EQ(foo.f(n, m, r, p, s, bar, v, z, b, sv, t), 0);\n",
        {});
    EXPECT_EQ(param_types(method(o, "Foo", "f", 11)),
              (std::vector<std::string>{"int", "long", "unsigned", "double*", "const std::string&", "const Bar&",
                                        "const std::vector<int>&", "std::size_t", "std::uint8_t", "std::string_view",
                                        "std::chrono::seconds"}));
}

TEST(InferenceParams, PointersToClassesArePassedAsPointers) {
    const Outcome o = body("Foo foo;\nBar* p = nullptr;\nconst Bar* q = nullptr;\nfoo.f(p, q);\nEXPECT_EQ(foo.f(p, q), 0);\n");
    EXPECT_EQ(param_types(method(o, "Foo", "f", 2)), (std::vector<std::string>{"Bar*", "const Bar*"}));
}

TEST(InferenceParams, AnArrayArgumentIsNotGuessed) {
    // `int values[3]` passed on may be a pointer or a reference to the array;
    // the text does not say which, so it is a question for the user.
    const Outcome o = body("Foo foo;\nint values[3] = {1, 2, 3};\nEXPECT_EQ(foo.f(values), 0);\n");
    EXPECT_EQ(errors(o), std::vector<std::string>{"mt-unknown-param"});
}

TEST(InferenceParams, EnumsAndConfiguredTypesAreByValue) {
    core::Config c;
    c.by_value_types = {"Handle"};
    const Outcome o = infer_text(
        "enum class Mode { On, Off };\n"
        "TEST(S, N) { Foo foo; Mode mode; Handle h; Other other;\n"
        "  EXPECT_EQ(foo.f(mode, h, other), 0); }\n", c);
    EXPECT_EQ(param_types(method(o, "Foo", "f", 3)), (std::vector<std::string>{"Mode", "Handle", "const Other&"}));
}

TEST(InferenceParams, MovedArgumentsAreTakenByValueWithANote) {
    const Outcome o = body("Foo foo;\nstd::string s;\nBar bar;\nfoo.take(std::move(s), std::move(bar));\n");
    EXPECT_EQ(param_types(method(o, "Foo", "take", 2)), (std::vector<std::string>{"std::string", "Bar"}));
    EXPECT_TRUE(has_note(method(o, "Foo", "take", 2).notes, "taken by value because the tests move into it"));
}

TEST(InferenceParams, AddressOfGivesAPointer) {
    const Outcome o = body("Foo foo;\nint n = 0;\nconst Bar bar;\nfoo.use(&n, &bar);\n");
    EXPECT_EQ(param_types(method(o, "Foo", "use", 2)), (std::vector<std::string>{"int*", "const Bar*"}));
}

TEST(InferenceParams, CastsGiveTheirTarget) {
    const Outcome o = body("Foo foo;\nfoo.put(static_cast<unsigned char>(5), (long)3, sizeof(int));\n");
    EXPECT_EQ(param_types(method(o, "Foo", "put", 3)), (std::vector<std::string>{"unsigned char", "long", "std::size_t"}));
}

TEST(InferenceParams, ParameterNamesComeFromVariablesOrArgN) {
    const Outcome o = body("Foo foo;\nint count = 0;\nint count2 = 0;\nfoo.f(count, 1, count2, 2);\nfoo.g(count, count);\n");
    EXPECT_EQ(param_names(method(o, "Foo", "f", 4)), (std::vector<std::string>{"count", "arg2", "count2", "arg4"}));
    EXPECT_EQ(param_names(method(o, "Foo", "g", 2)), (std::vector<std::string>{"count", "count2"})) << "no two parameters share a name";
}

TEST(InferenceParams, ANameFromAnyCallServes) {
    const Outcome o = body("Foo foo;\nint width = 0;\nfoo.f(1);\nfoo.f(width);\n");
    EXPECT_EQ(param_names(method(o, "Foo", "f", 1)), std::vector<std::string>{"width"});
}

TEST(InferenceParams, NullptrAndNothingKnownAreUnknownParameters) {
    const Outcome o = body("Foo foo;\nfoo.f(nullptr);\nfoo.g(mystery);\nfoo.h(other());\n");
    // The three parameters, and the result of other() that foo.h(other()) never uses.
    EXPECT_EQ(errors(o), (std::vector<std::string>{"mt-unknown-param", "mt-unknown-param", "mt-unknown-param", "mt-unknown-return"}));
}

TEST(InferenceParams, UnknownWithThePolicyTodo) {
    const Outcome o = body("Foo foo;\nfoo.f(nullptr);\n", todo_policy());
    EXPECT_TRUE(errors(o).empty());
    EXPECT_EQ(warnings(o), (std::vector<std::string>{"mt-unused-result", "mt-unknown-param"}));
    EXPECT_EQ(param_types(method(o, "Foo", "f", 1)), std::vector<std::string>{"int"});
    EXPECT_TRUE(has_note(method(o, "Foo", "f", 1).notes, "parameter 1 of 'Foo::f' is unknown; assumed 'int'"));
}

TEST(InferenceParams, AnotherCallSuppliesWhatOneCallLacks) {
    const Outcome o = body("Foo foo;\nfoo.f(nullptr);\nfoo.f(7);\n");
    EXPECT_TRUE(errors(o).empty());
    EXPECT_EQ(param_types(method(o, "Foo", "f", 1)), std::vector<std::string>{"int"});
}

TEST(InferenceParams, DisagreeingCallsAreOneMethodAndAConflict) {
    const Outcome o = body("Foo foo;\nfoo.f(1);\nfoo.f(2.5);\n");
    EXPECT_EQ(param_types(method(o, "Foo", "f", 1)), std::vector<std::string>{"int"});
    EXPECT_EQ(warnings(o), (std::vector<std::string>{"mt-unused-result", "mt-conflict"}));
}

TEST(InferenceParams, ADeclaredTypeBeatsALiteral) {
    const Outcome o = body("Foo foo;\nunsigned char b = 0;\nfoo.f(1);\nfoo.f(b);\n");
    EXPECT_EQ(param_types(method(o, "Foo", "f", 1)), std::vector<std::string>{"unsigned char"});
    EXPECT_EQ(method(o, "Foo", "f", 1).params[0].type.origin, ir::Origin::Declared);
}

TEST(InferenceParams, ParameterNotesSayWhatTheyWereInferredFrom) {
    const Outcome o = body("Foo foo;\nfoo.f(3u);\nint n = 0;\nfoo.g(n);\n");
    EXPECT_TRUE(has_note(method(o, "Foo", "f", 1).notes, "parameter 1 of 'Foo::f' inferred from literal 3u (t.cpp:4)"));
    for (const std::string& n : method(o, "Foo", "g", 1).notes) {
        EXPECT_EQ(n.find("parameter 1"), std::string::npos) << "a declared variable needs no note: " << n;
    }
}

// ---- const, static, overloads ---------------------------------------------------------------------------------------------------

TEST(InferenceShape, ConstOnlyWhenSomeUseIsOnAConstObject) {
    const Outcome o = body("Foo foo;\nconst Foo cfoo;\nconst Foo& ref = foo;\n"
                           "EXPECT_EQ(foo.a(), 1);\nEXPECT_EQ(cfoo.b(), 1);\nEXPECT_EQ(foo.c(), 1);\nEXPECT_EQ(ref.c(), 1);\n"
                           "EXPECT_EQ(foo.d(), 1);\nEXPECT_EQ(cfoo.d(), 1);\n");
    EXPECT_FALSE(method(o, "Foo", "a").is_const);
    EXPECT_TRUE(method(o, "Foo", "b").is_const);
    EXPECT_TRUE(method(o, "Foo", "c").is_const) << "one use on a const reference is enough";
    EXPECT_TRUE(method(o, "Foo", "d").is_const);
}

TEST(InferenceShape, ANonConstGetterGetsASecondLookNote) {
    const Outcome o = body("Foo foo;\nEXPECT_EQ(foo.size(), 3);\nEXPECT_TRUE(foo.empty());\nfoo.run();\nEXPECT_EQ(foo.add(1), 3);\n");
    EXPECT_TRUE(has_note(method(o, "Foo", "size").notes, "consider making this const"));
    EXPECT_TRUE(has_note(method(o, "Foo", "empty").notes, "consider making this const"));
    EXPECT_FALSE(has_note(method(o, "Foo", "run").notes, "consider making this const"));
    EXPECT_FALSE(has_note(method(o, "Foo", "add", 1).notes, "consider making this const")) << "it has arguments";
}

TEST(InferenceShape, StaticWhenCalledThroughTheClass) {
    const Outcome o = body("Foo foo;\nEXPECT_EQ(Foo::version(), 2u);\nEXPECT_EQ(foo.size(), 1);\nEXPECT_EQ(Foo::size(), 1);\n");
    EXPECT_TRUE(method(o, "Foo", "version").is_static);
    EXPECT_FALSE(method(o, "Foo", "version").is_const);
    EXPECT_TRUE(method(o, "Foo", "size").is_static) << "some use is X::m()";
}

TEST(InferenceShape, ConstOnAStaticUseDoesNotCount) {
    const Outcome o = body("const Foo c;\nEXPECT_EQ(c.size(), 1);\nEXPECT_EQ(Foo::size(), 1);\n");
    EXPECT_TRUE(method(o, "Foo", "size").is_static);
    EXPECT_FALSE(method(o, "Foo", "size").is_const) << "a static function is not const";
}

TEST(InferenceShape, ADifferentArityIsAnotherOverload) {
    const Outcome o = body("Foo foo;\nEXPECT_EQ(foo.f(1), 1);\nEXPECT_EQ(foo.f(1, 2), 1);\nEXPECT_EQ(foo.f(), 1);\n");
    EXPECT_NE(find_method(the_class(o, "Foo"), "f", 0), nullptr);
    EXPECT_NE(find_method(the_class(o, "Foo"), "f", 1), nullptr);
    EXPECT_NE(find_method(the_class(o, "Foo"), "f", 2), nullptr);
}

TEST(InferenceShape, TheOverloadAnnotationSeparatesByType) {
    const Outcome o = body("Foo foo;\nEXPECT_EQ(foo.f(1), 1);\n// @mt: overload\nEXPECT_EQ(foo.f(2.5), 1);\n");
    EXPECT_TRUE(warnings(o).empty());
    std::vector<std::string> types;
    for (const ir::Method& m : the_class(o, "Foo").methods) {
        if (m.name == "f") types.push_back(m.params[0].type.spelling);
    }
    EXPECT_EQ(types, (std::vector<std::string>{"int", "double"}));
}

TEST(InferenceShape, IgnoredCallsGenerateNothing) {
    const Outcome o = body("Foo foo;\nEXPECT_EQ(foo.a(), 1);\nfoo.b();  // @mt: ignore\n");
    EXPECT_EQ(find_method(the_class(o, "Foo"), "b", 0), nullptr);
    EXPECT_NE(find_method(the_class(o, "Foo"), "a", 0), nullptr);
}

TEST(InferenceShape, OperatorsAreOperators) {
    const Outcome o = body("Adder add;\nEXPECT_EQ(add(1, 2), 3);\n");
    const ir::Method& call = method(o, "Adder", "operator()", 2);
    EXPECT_EQ(call.kind, ir::MethodKind::Operator);
    EXPECT_EQ(call.ret.spelling, "int");
}

// ---- constructors ------------------------------------------------------------------------------------------------------------------------

TEST(InferenceConstructors, OnePerArityUsed) {
    const Outcome o = body("Foo a(1);\nFoo b(1, 2.5);\nFoo c(3);\n");
    const auto ctors = constructors(the_class(o, "Foo"));
    ASSERT_EQ(ctors.size(), 2u);
    EXPECT_EQ(param_types(*ctors[0]), std::vector<std::string>{"int"});
    EXPECT_EQ(param_types(*ctors[1]), (std::vector<std::string>{"int", "double"}));
    EXPECT_TRUE(ctors[0]->is_explicit);
    EXPECT_FALSE(ctors[1]->is_explicit);
}

TEST(InferenceConstructors, TheDefaultOneOnlyWhenAnotherExists) {
    EXPECT_TRUE(constructors(the_class(body("Foo a;\nFoo b;\n"), "Foo")).empty()) << "the implicit one is enough";
    const Outcome both = body("Foo a;\nFoo b(1);\n");
    const auto ctors = constructors(the_class(both, "Foo"));
    ASSERT_EQ(ctors.size(), 2u);
    EXPECT_TRUE(ctors[0]->params.empty());
    EXPECT_EQ(ctors[1]->params.size(), 1u);
}

TEST(InferenceConstructors, CopyInitialisationIsNotExplicit) {
    const Outcome o = body("Foo f = 5;\nBar b(5);\n");
    EXPECT_FALSE(constructors(the_class(o, "Foo")).front()->is_explicit);
    EXPECT_TRUE(constructors(the_class(o, "Bar")).front()->is_explicit);
}

TEST(InferenceConstructors, CopyAndMoveOfTheSameClassAreImplicit) {
    const Outcome o = body("Foo a(1);\nFoo b(a);\nFoo c(std::move(a));\nFoo d{b};\n");
    const auto ctors = constructors(the_class(o, "Foo"));
    ASSERT_EQ(ctors.size(), 1u);
    EXPECT_EQ(param_types(*ctors[0]), std::vector<std::string>{"int"});
}

TEST(InferenceConstructors, EveryFormCounts) {
    const Outcome o = body("Foo* p = new Foo(1);\nauto u = std::make_unique<Foo>(1, 2);\nFoo b{1, 2, 3};\nFoo g = Foo(1, 2, 3, 4);\n");
    std::vector<std::size_t> arities;
    for (const ir::Method* m : constructors(the_class(o, "Foo"))) arities.push_back(m->params.size());
    EXPECT_EQ(arities, (std::vector<std::size_t>{1, 2, 3, 4}));
}

TEST(InferenceConstructors, AFixtureMemberNeedsADefaultConstructorOnceThereIsAnother) {
    const Outcome o = infer_text(
        "class F : public ::testing::Test { protected: Foo foo_; };\n"
        "TEST_F(F, T) { Foo other(2); }\n");
    EXPECT_EQ(constructors(the_class(o, "Foo")).size(), 2u);
}

TEST(InferenceConstructors, ParametersAreInferredLikeAnyOther) {
    const Outcome o = body("int width = 3;\nFoo a(width, \"x\");\nFoo b(2, \"y\");\n");
    const ir::Method& ctor = *constructors(the_class(o, "Foo")).front();
    EXPECT_EQ(param_types(ctor), (std::vector<std::string>{"int", "const char*"}));
    EXPECT_EQ(param_names(ctor), (std::vector<std::string>{"width", "arg2"}));
}

// ---- fields and free functions -------------------------------------------------------------------------------------------------

TEST(InferenceFields, TypedByTheContext) {
    const Outcome o = body("Point p;\np.x = 5;\nEXPECT_EQ(p.y, 2.5);\nEXPECT_TRUE(p.ok);\nconst std::string& n = p.name;\n");
    const ir::Class& point = the_class(o, "Point");
    ASSERT_EQ(point.fields.size(), 4u);
    EXPECT_EQ(point.fields[0].name, "x");
    EXPECT_EQ(point.fields[0].type.spelling, "int");
    EXPECT_EQ(point.fields[1].type.spelling, "double");
    EXPECT_EQ(point.fields[2].type.spelling, "bool");
    EXPECT_EQ(point.fields[3].type.spelling, "std::string") << "a field is a value, not a reference";
    EXPECT_EQ(point.fields[0].access, ir::Access::Public);
}

TEST(InferenceFields, NotesSayWhereTheTypeCameFrom) {
    const Outcome o = body("Point p;\np.x = 5;\nint n = p.y;\n");
    const ir::Class& point = the_class(o, "Point");
    ASSERT_EQ(point.fields.size(), 2u);
    EXPECT_EQ(point.fields[0].notes, std::vector<std::string>{"type inferred from literal 5 (t.cpp:4)"});
    EXPECT_TRUE(point.fields[1].notes.empty()) << "declared by the user";
}

TEST(InferenceFields, AFieldIsNeverVoid) {
    const Outcome o = body("Point p;\np.x;\n");
    EXPECT_EQ(errors(o), std::vector<std::string>{"mt-unknown-return"});
    EXPECT_FALSE(the_class(o, "Point").fields[0].type.is_void());
}

TEST(InferenceFields, AFieldNothingTypesIsUnknown) {
    const Outcome o = body("Point p;\nEXPECT_EQ(p.x, p.y);\n");
    EXPECT_EQ(errors(o), (std::vector<std::string>{"mt-unknown-return", "mt-unknown-return"}));
}

TEST(InferenceFunctions, TypedLikeMethods) {
    const Outcome o = body("EXPECT_EQ(add(1, 2), 3);\nint a = 5;\nEXPECT_TRUE(is_even(a));\nreset();\n");
    ASSERT_EQ(o.model.functions.size(), 3u);
    EXPECT_EQ(o.model.functions[0].name, "add");
    EXPECT_EQ(o.model.functions[0].ret.spelling, "int");
    EXPECT_EQ(o.model.functions[0].params.size(), 2u);
    EXPECT_EQ(o.model.functions[1].name, "is_even");
    EXPECT_EQ(o.model.functions[1].ret.spelling, "bool");
    EXPECT_EQ(o.model.functions[1].params[0].name, "a");
    EXPECT_TRUE(o.model.functions[2].ret.is_void());
}

TEST(InferenceFunctions, NamespacesAndOverloadsByArity) {
    const Outcome o = infer_text("namespace util { TEST(S, N) { EXPECT_EQ(twice(2), 4); EXPECT_EQ(twice(2, 3), 5); } }\n");
    ASSERT_EQ(o.model.functions.size(), 2u);
    EXPECT_EQ(o.model.functions[0].ns, std::vector<std::string>{"util"});
    EXPECT_EQ(o.model.functions[0].name, "twice");
    EXPECT_EQ(o.model.functions[1].name, "twice");
    EXPECT_NE(o.model.functions[0].params.size(), o.model.functions[1].params.size());
}

TEST(InferenceFunctions, AFunctionOfANamespaceCalledWithAScope) {
    const Outcome o = body("acme::Foo foo;\nEXPECT_EQ(acme::build(1), 2u);\n");
    ASSERT_EQ(o.model.functions.size(), 1u);
    EXPECT_EQ(o.model.functions[0].ns, std::vector<std::string>{"acme"});
    EXPECT_EQ(o.model.functions[0].ret.spelling, "unsigned int");
}

// ---- annotations and configuration ---------------------------------------------------------------------------------------------------

TEST(InferenceAnnotations, AShortReturnsBeatsTheContext) {
    const Outcome o = body("Foo foo;\n// @mt: returns long\nEXPECT_EQ(foo.m(), 3);\n");
    EXPECT_EQ(method(o, "Foo", "m").ret.spelling, "long");
    EXPECT_EQ(method(o, "Foo", "m").ret.origin, ir::Origin::Annotated);
    EXPECT_FALSE(has_note(method(o, "Foo", "m").notes, "inferred")) << "the user said so";
    EXPECT_TRUE(warnings(o).empty()) << "the annotation is on this very statement: no disagreement to report";
}

TEST(InferenceAnnotations, AShortReturnsAnswersWhatNothingElseCould) {
    const Outcome o = body("Foo foo;\n// @mt: returns std::vector<int>\nauto items = foo.items();\n");
    EXPECT_TRUE(errors(o).empty());
    EXPECT_EQ(method(o, "Foo", "items").ret.spelling, "std::vector<int>");
}

TEST(InferenceAnnotations, TheLongFormsOverrideEverything) {
    const Outcome o = infer_text(
        "// @mt: Foo::items returns std::vector<int>; Foo::set param 1 unsigned long; Foo::size const; "
        "Foo::make static; Foo::run virtual; Foo::stop noexcept\n"
        "TEST(S, N) { Foo foo;\n"
        "  auto items = foo.items();\n  foo.set(1);\n  EXPECT_EQ(foo.size(), 1);\n"
        "  EXPECT_EQ(foo.make(), 1);\n  foo.run();\n  foo.stop();\n}\n");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    EXPECT_EQ(method(o, "Foo", "items").ret.spelling, "std::vector<int>");
    EXPECT_EQ(param_types(method(o, "Foo", "set", 1)), std::vector<std::string>{"unsigned long"});
    EXPECT_TRUE(method(o, "Foo", "size").is_const);
    EXPECT_TRUE(method(o, "Foo", "make").is_static);
    EXPECT_TRUE(method(o, "Foo", "run").is_virtual);
    EXPECT_TRUE(method(o, "Foo", "stop").is_noexcept);
}

TEST(InferenceAnnotations, ConstFalseInTheConfigurationBeatsAConstUse) {
    core::Config c;
    ASSERT_TRUE(core::apply_config_json(c, R"({"overrides": {"Foo::size": {"const": false}}})", "cfg", *std::make_unique<core::Diagnostics>()));
    const Outcome o = body("const Foo foo;\nEXPECT_EQ(foo.size(), 1);\n", c);
    EXPECT_FALSE(method(o, "Foo", "size").is_const);
}

TEST(InferenceAnnotations, AConstructorTakesItsParametersFromTheNamedMember) {
    const Outcome o = infer_text("// @mt: Foo::Foo param 1 unsigned long\nTEST(S, N) { Foo foo(1); }\n");
    EXPECT_EQ(param_types(*constructors(the_class(o, "Foo")).front()), std::vector<std::string>{"unsigned long"});
}

TEST(InferenceAnnotations, AnAnnotatedTypeIsNeverMapped) {
    core::Config c;
    c.type_map = {{"unsigned int", "std::uint32_t"}};
    const Outcome o = body("Foo foo;\n// @mt: returns unsigned int\nEXPECT_EQ(foo.a(), 1u);\n", c);
    EXPECT_EQ(method(o, "Foo", "a").ret.spelling, "unsigned int");
}

TEST(InferenceConfig, TheTypeMapRewritesWhatWasInferred) {
    core::Config c;
    c.type_map = {{"unsigned int", "std::uint32_t"}, {"unsigned", "std::uint32_t"}};
    // `unsigned int` and `unsigned` are one type: both keys address it, so two
    // keys for it would be ambiguous -- the map in config rejects such a pair.
    c.type_map.erase("unsigned");
    const Outcome o = body("Foo foo;\nEXPECT_EQ(foo.a(), 3u);\nunsigned n = 0;\nEXPECT_EQ(foo.b(), n);\nfoo.c(4u);\n", c);
    EXPECT_EQ(method(o, "Foo", "a").ret.spelling, "std::uint32_t") << "inferred from a literal: rewritten";
    EXPECT_EQ(method(o, "Foo", "b").ret.spelling, "unsigned") << "declared by the user: left alone";
    EXPECT_EQ(param_types(method(o, "Foo", "c", 1)), std::vector<std::string>{"std::uint32_t"});
}

TEST(InferenceConfig, TheTypeMapKeepsPointersAndConstness) {
    core::Config c;
    c.type_map = {{"char", "std::int8_t"}};
    const Outcome o = body("Foo foo;\nEXPECT_STREQ(foo.name(), \"x\");\n", c);
    EXPECT_EQ(method(o, "Foo", "name").ret.spelling, "const std::int8_t*");
}

TEST(InferenceConfig, TypesThatNeedANewerStandardAreWarnedAbout) {
    core::Config c;
    c.cxx_std = core::CxxStd::Cxx14;
    const Outcome old_std = body("Foo foo;\n// @mt: returns std::optional<int>\nEXPECT_TRUE(foo.m());\n", c);
    EXPECT_EQ(warnings(old_std), std::vector<std::string>{"mt-std"});
    const Outcome new_std = body("Foo foo;\n// @mt: returns std::optional<int>\nEXPECT_TRUE(foo.m());\n");
    EXPECT_TRUE(warnings(new_std).empty() || warnings(new_std) == std::vector<std::string>{"mt-conflict"});
    for (const std::string& w : warnings(new_std)) EXPECT_NE(w, "mt-std");
}

// ---- the whole --------------------------------------------------------------------------------------------------------------------------------

TEST(InferenceWhole, ASmallRealisticFile) {
    const Outcome o = infer_text(
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
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
    EXPECT_EQ(param_types(method(o, "Calculator", "add", 2)), (std::vector<std::string>{"int", "int"}));
    EXPECT_EQ(method(o, "Calculator", "add", 2).ret.spelling, "int");
    EXPECT_TRUE(method(o, "Calculator", "reset").ret.is_void());
    EXPECT_TRUE(method(o, "Calculator", "version").is_static);
    EXPECT_EQ(o.model.functions.size(), 1u);
    EXPECT_EQ(o.model.functions[0].name, "clamp_to_byte");
    ASSERT_EQ(constructors(the_class(o, "Display")).size(), 1u);
    EXPECT_EQ(param_types(*constructors(the_class(o, "Display")).front()), std::vector<std::string>{"const Calculator&"});
}

TEST(InferenceWhole, TheSameInputGivesTheSameModelAndDiagnostics) {
    const std::string text =
        "TEST(S, N) { Foo foo; foo.a(); foo.a(1); EXPECT_EQ(foo.b(1), 2); bar(foo.c()); auto x = foo.d(); Widget::make(1); }\n";
    const Outcome a = infer_text(text);
    const Outcome b = infer_text(text);
    ASSERT_EQ(a.model.classes.size(), b.model.classes.size());
    for (std::size_t i = 0; i < a.model.classes.size(); ++i) {
        ASSERT_EQ(a.model.classes[i].methods.size(), b.model.classes[i].methods.size());
        for (std::size_t j = 0; j < a.model.classes[i].methods.size(); ++j) {
            EXPECT_EQ(a.model.classes[i].methods[j].name, b.model.classes[i].methods[j].name);
            EXPECT_EQ(a.model.classes[i].methods[j].ret.spelling, b.model.classes[i].methods[j].ret.spelling);
            EXPECT_EQ(a.model.classes[i].methods[j].notes, b.model.classes[i].methods[j].notes);
        }
    }
    EXPECT_EQ(a.diagnostics.sorted(), b.diagnostics.sorted());
}

// ---- using namespace std ----------------------------------------------------------------------------------

TEST(InferenceStd, ADeclaredTypeGetsItsPrefixBackBecauseAHeaderHasNoUsingDirective) {
    const Outcome o = infer_text(
        "using namespace std;\n"
        "TEST(S, N) {\n"
        "    Foo foo(1);\n"
        "    string s = foo.name();\n"
        "    vector<string> v = foo.items();\n"
        "}\n");
    EXPECT_EQ(method(o, "Foo", "name").ret.spelling, "std::string");
    EXPECT_EQ(method(o, "Foo", "items").ret.spelling, "std::vector<std::string>");
    EXPECT_EQ(method(o, "Foo", "name").ret.origin, ir::Origin::Declared);
}

TEST(InferenceStd, ParametersAreQualifiedToo) {
    const Outcome o = infer_text(
        "using namespace std;\n"
        "TEST(S, N) {\n"
        "    string s(\"x\");\n"
        "    Foo foo(1);\n"
        "    foo.set(s);\n"
        "}\n");
    EXPECT_EQ(param_types(method(o, "Foo", "set", 1)), std::vector<std::string>{"const std::string&"});
}

TEST(InferenceStd, WithoutTheDirectiveNothingChanges) {
    const Outcome o = infer_text(
        "TEST(S, N) {\n"
        "    Foo foo(1);\n"
        "    string s = foo.name();\n"
        "}\n");
    EXPECT_EQ(method(o, "Foo", "name").ret.spelling, "string");
}

TEST(InferenceStd, AQualifiedNameIsLeftAlone) {
    const Outcome o = infer_text(
        "using namespace std;\n"
        "TEST(S, N) {\n"
        "    Foo foo(1);\n"
        "    std::string a = foo.a();\n"
        "    other::string b = foo.b();\n"
        "    ::string c = foo.c();\n"
        "}\n");
    EXPECT_EQ(method(o, "Foo", "a").ret.spelling, "std::string");
    EXPECT_EQ(method(o, "Foo", "b").ret.spelling, "other::string");
    EXPECT_EQ(method(o, "Foo", "c").ret.spelling, "::string");
}

TEST(InferenceStd, TypeMapStillWinsAndIsWrittenVerbatim) {
    core::Config config;
    config.type_map["double"] = "vector<Money>";
    const Outcome o = infer_text(
        "using namespace std;\n"
        "TEST(S, N) {\n"
        "    Foo foo(1);\n"
        "    EXPECT_EQ(foo.total(), 2.5);\n"
        "}\n",
        config);
    EXPECT_EQ(method(o, "Foo", "total").ret.spelling, "vector<Money>");
}

TEST(InferenceCasts, AComparisonWithACastToAStandardTypeSaysWhatTheResultIs) {
    const Outcome o = body("Foo foo;\nEXPECT_EQ(foo.name(), std::string(\"x\"));\nEXPECT_EQ(foo.label(), QString(\"y\"));\n");
    EXPECT_EQ(method(o, "Foo", "name").ret.spelling, "std::string");
    EXPECT_EQ(method(o, "Foo", "label").ret.spelling, "QString");
    EXPECT_TRUE(errors(o).empty()) << ::testing::PrintToString(errors(o));
}

TEST(InferenceCasts, ACastToAClassIsTakenByConstReferenceWhateverItsOrigin) {
    const Outcome o = body("Foo foo;\nfoo.put(std::string(\"x\"), std::size_t(2), QString(\"q\"));\n");
    EXPECT_EQ(param_types(method(o, "Foo", "put", 3)), (std::vector<std::string>{"const std::string&", "std::size_t", "const QString&"}));
}

TEST(InferenceWhole, NothingToInferIsAnEmptyModel) {
    const Outcome o = infer_text("");
    EXPECT_TRUE(o.model.classes.empty());
    EXPECT_TRUE(o.model.functions.empty());
}

TEST(InferenceWhole, EveryPrefixOfAFileIsInferredWithoutIncident) {
    const std::string text =
        "// @mt: Foo::f returns long\n"
        "TEST(S, N) {\n"
        "    Foo foo(3, \"abc\");\n"
        "    EXPECT_EQ(foo.size(), 3u);\n"
        "    auto n = foo.f(1);\n"
        "    EXPECT_EQ(n, 5);\n"
        "    foo.make().run();\n"
        "    Foo::create(1.0f);\n"
        "    bar(foo.g());  // @mt: ignore\n"
        "}\n";
    for (std::size_t n = 0; n <= text.size(); ++n) {
        const Outcome o = infer_text(text.substr(0, n));
        EXPECT_LE(o.model.classes.size(), 3u) << "cut at " << n;
    }
}

// ---- test doubles --------------------------------------------------------------------------------------------------------

namespace {

const char* const kFakeSensor =
    "class FakeSensor : public Sensor {\n"
    "public:\n"
    "    int read() override { return 5; }\n"
    "    void set(double v, const std::string& unit) override {}\n"
    "    bool ok() const override { return true; }\n"
    "};\n";

std::vector<std::string> signatures(const ir::Class& c) {
    std::vector<std::string> out;
    for (const ir::Method& m : c.methods) {
        std::string text;
        if (m.is_virtual) text += "virtual ";
        if (m.kind == ir::MethodKind::Constructor || m.kind == ir::MethodKind::Destructor) text += m.name;
        else text += m.ret.spelling + " " + m.name;
        text += "(";
        for (std::size_t i = 0; i < m.params.size(); ++i) text += (i == 0 ? "" : ", ") + m.params[i].type.spelling;
        text += std::string(")") + (m.is_const ? " const" : "") + (m.is_pure ? " = 0" : "") + (m.is_defaulted ? " = default" : "");
        out.push_back(text);
    }
    return out;
}

}  // namespace

TEST(InferenceDoubles, TheBaseOfAFakeIsAnInterface) {
    const Outcome o = infer_text(std::string(kFakeSensor) + "TEST(S, N) { FakeSensor f; }\n");
    EXPECT_EQ(signatures(the_class(o, "Sensor")),
              (std::vector<std::string>{"virtual ~Sensor() = default", "virtual int read() = 0",
                                        "virtual void set(double, const std::string&) = 0", "virtual bool ok() const = 0"}));
    EXPECT_TRUE(errors(o).empty());
    EXPECT_TRUE(warnings(o).empty());
}

TEST(InferenceDoubles, TheVirtualDestructorSaysWhy) {
    const Outcome o = infer_text(std::string(kFakeSensor));
    const ir::Method& destructor = the_class(o, "Sensor").methods.front();
    ASSERT_EQ(destructor.kind, ir::MethodKind::Destructor);
    EXPECT_EQ(destructor.name, "~Sensor");
    EXPECT_TRUE(destructor.is_virtual);
    EXPECT_TRUE(destructor.is_defaulted);
    EXPECT_EQ(destructor.origin, ir::Origin::Inferred);
    EXPECT_EQ(destructor.where.line, 1u) << "where the interface was first mentioned: the base of the fake";
    ASSERT_EQ(destructor.notes.size(), 1u);
    EXPECT_EQ(destructor.notes[0], "virtual because the test derives 'FakeSensor' from it");
}

TEST(InferenceDoubles, TheNoteNamesEveryDouble) {
    const Outcome o = infer_text(
        "class FakeSensor : public Sensor { public: int read() override { return 1; } };\n"
        "class MockSensor : public Sensor { public: MOCK_METHOD(int, read, (), (override)); };\n");
    EXPECT_EQ(the_class(o, "Sensor").methods.front().notes,
              std::vector<std::string>{"virtual because the test derives 'FakeSensor', 'MockSensor' from it"});
}

TEST(InferenceDoubles, TheDeclaredSignatureIsKeptAsDeclared) {
    const Outcome o = infer_text(std::string(kFakeSensor));
    const ir::Method& set = method(o, "Sensor", "set", 2);
    EXPECT_EQ(param_types(set), (std::vector<std::string>{"double", "const std::string&"}));
    EXPECT_EQ(param_names(set), (std::vector<std::string>{"v", "unit"}));
    EXPECT_EQ(set.origin, ir::Origin::Declared);
    EXPECT_EQ(set.ret.spelling, "void");
    EXPECT_TRUE(set.notes.empty()) << "nothing is guessed here";
    EXPECT_EQ(set.access, ir::Access::Public);
}

TEST(InferenceDoubles, AMockSaysTheSame) {
    const Outcome o = infer_text(
        "class MockSensor : public Sensor {\n"
        "public:\n"
        "    MOCK_METHOD(int, read, (), (override));\n"
        "    MOCK_METHOD(std::string, name, (), (const, override));\n"
        "    MOCK_METHOD1(write, void(int));\n"
        "};\n");
    EXPECT_EQ(signatures(the_class(o, "Sensor")),
              (std::vector<std::string>{"virtual ~Sensor() = default", "virtual int read() = 0", "virtual std::string name() const = 0",
                                        "virtual void write(int) = 0"}));
}

TEST(InferenceDoubles, AParameterThatReceivesAPointerToTheDoubleHasTheInterfaceType) {
    const Outcome o = infer_text(std::string(kFakeSensor) + "TEST(S, N) { FakeSensor f; Controller c(&f); }\n");
    const auto ctors = constructors(the_class(o, "Controller"));
    ASSERT_EQ(ctors.size(), 1u);
    EXPECT_EQ(param_types(*ctors[0]), std::vector<std::string>{"Sensor*"});
    EXPECT_EQ(param_names(*ctors[0]), std::vector<std::string>{"f"});
}

TEST(InferenceDoubles, AndAReferenceToIt) {
    const Outcome o = infer_text(std::string(kFakeSensor) + "TEST(S, N) { FakeSensor f; Controller c; c.attach(f); }\n");
    EXPECT_EQ(param_types(method(o, "Controller", "attach", 1)), std::vector<std::string>{"const Sensor&"});
}

TEST(InferenceDoubles, TheWrappersOfGmockAreTheMockTheyWrap) {
    const Outcome o = infer_text(
        "class MockSensor : public Sensor { public: MOCK_METHOD(int, read, (), (override)); };\n"
        "TEST(S, N) {\n"
        "    testing::NiceMock<MockSensor> a;\n"
        "    ::testing::StrictMock<MockSensor> b;\n"
        "    testing::NaggyMock<MockSensor> c;\n"
        "    Controller k;\n"
        "    k.one(&a);\n"
        "    k.two(&b);\n"
        "    k.three(&c);\n"
        "    k.four(a);\n"
        "}\n");
    EXPECT_EQ(param_types(method(o, "Controller", "one", 1)), std::vector<std::string>{"Sensor*"});
    EXPECT_EQ(param_types(method(o, "Controller", "two", 1)), std::vector<std::string>{"Sensor*"});
    EXPECT_EQ(param_types(method(o, "Controller", "three", 1)), std::vector<std::string>{"Sensor*"});
    EXPECT_EQ(param_types(method(o, "Controller", "four", 1)), std::vector<std::string>{"const Sensor&"});
    EXPECT_TRUE(errors(o).empty());
    for (const std::string& code : warnings(o)) EXPECT_EQ(code, "mt-unused-result") << "no class template NiceMock, no conflict";
}

TEST(InferenceDoubles, AWrapperOfSomethingThatIsNotADoubleIsLeftAlone) {
    const Outcome o = infer_text(
        "TEST(S, N) { testing::NiceMock<RealMock> a; Controller k; k.one(&a); }\n");
    EXPECT_EQ(param_types(method(o, "Controller", "one", 1)), std::vector<std::string>{"testing::NiceMock<RealMock>*"});
}

TEST(InferenceDoubles, ADoubleInsideATemplateArgumentIsReplacedToo) {
    const Outcome o = infer_text(
        std::string(kFakeSensor) +
        "TEST(S, N) { std::unique_ptr<FakeSensor> p; Controller k; k.adopt(std::move(p)); std::vector<FakeSensor*> v; k.all(v); }\n");
    EXPECT_EQ(param_types(method(o, "Controller", "adopt", 1)), std::vector<std::string>{"std::unique_ptr<Sensor>"});
    EXPECT_EQ(param_types(method(o, "Controller", "all", 1)), std::vector<std::string>{"const std::vector<Sensor*>&"});
}

TEST(InferenceDoubles, TheNamesAroundADoubleAreKept) {
    const Outcome o = infer_text(
        std::string(kFakeSensor) +
        "TEST(S, N) { std::map<std::string, FakeSensor*> m; Controller k; k.all(m); }\n");
    EXPECT_EQ(param_types(method(o, "Controller", "all", 1)), std::vector<std::string>{"const std::map<std::string, Sensor*>&"});
}

TEST(InferenceDoubles, TheSubstitutedTypeKeepsWhereItWasWrittenAndWhy) {
    const std::string text = std::string(kFakeSensor) + "TEST(S, N) { FakeSensor f; Controller c(&f); Foo foo; Controller d(&foo); }\n";
    const Outcome o = infer_text(text);
    const auto ctors = constructors(the_class(o, "Controller"));
    ASSERT_EQ(ctors.size(), 1u);
    const ir::Type& t = ctors[0]->params[0].type;
    EXPECT_EQ(t.spelling, "Sensor*");
    EXPECT_EQ(t.origin, ir::Origin::Declared);
    EXPECT_GT(t.where.line, 0u);
    EXPECT_FALSE(t.reason.empty());
    // The same arguments with no double in the way carry the same kind of provenance.
    const Outcome plain = infer_text("TEST(S, N) { Foo foo; Controller d(&foo); }\n");
    const ir::Type& p = constructors(the_class(plain, "Controller"))[0]->params[0].type;
    EXPECT_EQ(p.origin, t.origin);
    EXPECT_EQ(p.reason.empty(), t.reason.empty());
}

TEST(InferenceDoubles, ANameThatMerelyContainsTheDoublesNameIsNotReplaced) {
    const Outcome o = infer_text(
        std::string(kFakeSensor) +
        "TEST(S, N) { FakeSensorFactory f; Controller k; k.take(&f); ns::FakeSensors g; k.more(&g); }\n");
    EXPECT_EQ(param_types(method(o, "Controller", "take", 1)), std::vector<std::string>{"FakeSensorFactory*"});
    EXPECT_EQ(param_types(method(o, "Controller", "more", 1)), std::vector<std::string>{"ns::FakeSensors*"});
}

TEST(InferenceDoubles, AQualifiedNameOfADoubleIsReplacedWhole) {
    const Outcome o = infer_text(
        std::string(kFakeSensor) + "TEST(S, N) { FakeSensor f; Controller k; k.take(&f); }\n"
        "namespace t { class Fake : public acme::Sensor { public: int read() override { return 1; } }; }\n"
        "TEST(S, M) { t::Fake f; Controller k; k.put(&f); }\n");
    EXPECT_EQ(param_types(method(o, "Controller", "put", 1)), std::vector<std::string>{"acme::Sensor*"});
}

TEST(InferenceDoubles, TwoDoublesPassedToOneParameterAgree) {
    const Outcome o = infer_text(
        "class Fake : public Sensor { public: int read() override { return 1; } };\n"
        "class Mock : public Sensor { public: MOCK_METHOD(int, read, (), (override)); };\n"
        "TEST(S, A) { Fake f; Controller c(&f); }\n"
        "TEST(S, B) { testing::NiceMock<Mock> m; Controller c(&m); }\n");
    const auto ctors = constructors(the_class(o, "Controller"));
    ASSERT_EQ(ctors.size(), 1u);
    EXPECT_EQ(param_types(*ctors[0]), std::vector<std::string>{"Sensor*"});
    EXPECT_TRUE(warnings(o).empty()) << "one type, not a conflict between two doubles";
}

TEST(InferenceDoubles, ACallThroughTheInterfaceGivesWayToTheDeclaredSignature) {
    const Outcome o = infer_text(
        std::string(kFakeSensor) +
        "TEST(S, N) { FakeSensor f; Sensor* s = &f; auto r = s->read(); EXPECT_EQ(s->read(), 7L); s->read(); }\n");
    std::size_t reads = 0;
    for (const ir::Method& m : the_class(o, "Sensor").methods) reads += m.name == "read" ? 1 : 0;
    EXPECT_EQ(reads, 1u);
    EXPECT_EQ(method(o, "Sensor", "read").ret.spelling, "int") << "the double says int; the test only compares";
    EXPECT_TRUE(method(o, "Sensor", "read").is_pure);
}

TEST(InferenceDoubles, ACallWithAnotherArityIsAnotherFunction) {
    const Outcome o = infer_text(
        "class Fake : public Sensor { public: void put(int v) override {} };\n"
        "TEST(S, N) { Fake f; Sensor* s = &f; s->put(1, 2); }\n");
    EXPECT_NE(find_method(the_class(o, "Sensor"), "put", 1), nullptr);
    EXPECT_NE(find_method(the_class(o, "Sensor"), "put", 2), nullptr);
    EXPECT_TRUE(find_method(the_class(o, "Sensor"), "put", 1)->is_pure);
    EXPECT_FALSE(find_method(the_class(o, "Sensor"), "put", 2)->is_pure);
}

TEST(InferenceDoubles, UsingNamespaceStdIsSpelledOutInTheInterface) {
    const Outcome o = infer_text(
        "using namespace std;\n"
        "class Fake : public Sensor { public: string name() const override { return \"\"; } void set(const vector<int>& v) override {} };\n");
    EXPECT_EQ(signatures(the_class(o, "Sensor")),
              (std::vector<std::string>{"virtual ~Sensor() = default", "virtual std::string name() const = 0",
                                        "virtual void set(const std::vector<int>&) = 0"}));
}

TEST(InferenceDoubles, ADoubleOfAKnownClassStillGivesTheInterfaceType) {
    std::vector<ir::Model> known;
    core::Diagnostics diag;
    known.push_back(frontend::read_header({"sensor.h", "class Sensor { public: virtual ~Sensor(); virtual int read() = 0; };\n"},
                                          "sensor.h", {}, diag));
    const Outcome o = infer_text(std::string(kFakeSensor) + "TEST(S, N) { FakeSensor f; Controller c(&f); }\n", {}, known);
    ASSERT_EQ(o.model.classes.size(), 1u);
    EXPECT_EQ(o.model.classes[0].name, "Controller");
    EXPECT_EQ(param_types(*constructors(o.model.classes[0])[0]), std::vector<std::string>{"Sensor*"});
}

TEST(InferenceDoubles, ADoubleOfSomebodyElsesClassGivesItsType) {
    const Outcome o = infer_text(
        "class MyError : public std::runtime_error { public: MyError() : std::runtime_error(\"x\") {} };\n"
        "TEST(S, N) { MyError e; Handler h; h.handle(e); }\n");
    EXPECT_EQ(param_types(method(o, "Handler", "handle", 1)), std::vector<std::string>{"const std::runtime_error&"});
}

TEST(InferenceDoubles, ALocalClassWithoutABaseKeepsItsOwnType) {
    const Outcome o = infer_text(
        "struct Params { int a; };\n"
        "TEST(S, N) { Params p; Controller c(p); }\n");
    EXPECT_EQ(param_types(*constructors(the_class(o, "Controller"))[0]), std::vector<std::string>{"const Params&"});
}

TEST(InferenceDoubles, ANonInterfaceClassHasNoVirtualDestructor) {
    const Outcome o = body("Foo f(1); f.run();\n");
    for (const ir::Method& m : the_class(o, "Foo").methods) EXPECT_NE(m.kind, ir::MethodKind::Destructor);
}

TEST(InferenceDoubles, AClassTheTestAlsoBuildsIsNotAbstract) {
    // The "testable subclass" pattern: Foo is built on its own, so its virtual functions get stubs.
    const Outcome o = infer_text(
        "class TestableFoo : public Foo { public: int hook() override { return 1; } };\n"
        "TEST(S, N) { Foo foo; TestableFoo t; EXPECT_EQ(foo.size(), 3u); }\n");
    EXPECT_EQ(signatures(the_class(o, "Foo")),
              (std::vector<std::string>{"virtual ~Foo() = default", "unsigned int size()", "virtual int hook()"}));
}

TEST(InferenceDoubles, AClassTheTestBuildsThroughANewExpressionIsNotAbstractEither) {
    const Outcome o = infer_text(
        "class TestableFoo : public Foo { public: int hook() override { return 1; } };\n"
        "TEST(S, N) { Foo* foo = new Foo(); delete foo; }\n");
    EXPECT_FALSE(method(o, "Foo", "hook").is_pure);
}

TEST(InferenceDoubles, TheReturnTypeOfAFunctionThatGivesBackADoubleIsTheInterface) {
    const Outcome o = infer_text(
        std::string(kFakeSensor) +
        "TEST(S, N) { FakeSensor f; Controller c(&f); EXPECT_EQ(c.sensor(), &f); }\n");
    EXPECT_EQ(method(o, "Controller", "sensor").ret.spelling, "Sensor*");
}

TEST(InferenceDoubles, AnyPrefixOfAFileWithDoublesIsRead) {
    // Cut anywhere, a file with mocks, wrappers and a fixture is read without incident: the macros
    // are expanded by text, which a cut leaves half written.
    const std::string text =
        "#include <gmock/gmock.h>\n"
        "using ::testing::NiceMock;\n"
        "namespace {\n"
        "class MockSensor : public Sensor {\n"
        "public:\n"
        "    MOCK_METHOD(int, read, (), (override));\n"
        "    MOCK_METHOD(void,\n        put,\n        ((std::map<int, int>), int),\n        (const, override));\n"
        "    MOCK_CONST_METHOD1(at, const int&(int));\n"
        "};\n"
        "}  // namespace\n"
        "class F : public ::testing::Test { protected: NiceMock<MockSensor> s; Controller c{&s}; };\n"
        "TEST_F(F, N) { EXPECT_EQ(c.value(), 42); }\n";
    for (std::size_t n = 0; n <= text.size(); ++n) {
        const Outcome o = infer_text(text.substr(0, n));
        EXPECT_LE(o.model.classes.size(), 2u) << "cut at " << n;
    }
    EXPECT_EQ(infer_text(text).model.classes.size(), 2u);
}

TEST(InferenceDoubles, AnInterfaceWithoutOverridesIsStillGenerated) {
    const Outcome o = infer_text("class Fake : public Sensor {};\nTEST(S, N) { Fake f; }\n");
    EXPECT_EQ(signatures(the_class(o, "Sensor")), std::vector<std::string>{});
}

// ---- operators of the objects a test compares and indexes ----------------------------------------------------------------

TEST(InferenceOperators, ComparingTwoObjectsAsksForAConstOperatorReturningBool) {
    const Outcome o = body("Point a(1, 2);\nPoint b(1, 2);\nEXPECT_TRUE(a == b);\n");
    const ir::Method& m = method(o, "Point", "operator==", 1);
    EXPECT_EQ(m.kind, ir::MethodKind::Operator);
    EXPECT_EQ(m.ret.spelling, "bool");
    EXPECT_TRUE(m.is_const) << "comparing does not change the object, even when the test did it through a non-const one";
    EXPECT_EQ(param_types(m), std::vector<std::string>{"const Point&"});
}

TEST(InferenceOperators, EveryComparisonIsConstAndBool) {
    const Outcome o = body("Point a;\nPoint b;\nEXPECT_NE(a, b);\nEXPECT_LT(a, b);\nEXPECT_LE(a, b);\nEXPECT_GT(a, b);\nEXPECT_GE(a, b);\n");
    for (const char* name : {"operator!=", "operator<", "operator<=", "operator>", "operator>="}) {
        EXPECT_EQ(method(o, "Point", name, 1).ret.spelling, "bool") << name;
        EXPECT_TRUE(method(o, "Point", name, 1).is_const) << name;
    }
}

TEST(InferenceOperators, TheOtherOperandCanBeANumber) {
    const Outcome o = body("Meter m;\nEXPECT_EQ(m, 5);\nEXPECT_TRUE(m > 2.5);\n");
    EXPECT_EQ(param_types(method(o, "Meter", "operator==", 1)), std::vector<std::string>{"int"});
    EXPECT_EQ(param_types(method(o, "Meter", "operator>", 1)), std::vector<std::string>{"double"});
}

TEST(InferenceOperators, TheUsesOfOneOperatorAreOneMethod) {
    const Outcome o = body("Point a;\nPoint b;\nEXPECT_TRUE(a == b);\nEXPECT_EQ(a, b);\nASSERT_EQ(b, a);\n");
    int count = 0;
    for (const ir::Method& m : the_class(o, "Point").methods) count += m.name == "operator==" ? 1 : 0;
    EXPECT_EQ(count, 1);
}

TEST(InferenceOperators, TheResultOfACallComparedWithAnObjectIsThatObjectsType) {
    const Outcome o = body("Sensor sensor;\nPoint expected;\nEXPECT_EQ(sensor.position(), expected);\n");
    EXPECT_EQ(method(o, "Sensor", "position").ret.spelling, "Point");
    EXPECT_EQ(method(o, "Point", "operator==", 1).ret.spelling, "bool");
}

TEST(InferenceOperators, NoClassIsMadeOfTheStandardLibrarysOperands) {
    const Outcome o = body("std::string a;\nstd::string b;\nEXPECT_TRUE(a == b);\nstd::vector<int> v;\nEXPECT_EQ(v[0], 1);\n");
    EXPECT_TRUE(o.model.classes.empty());
}

TEST(InferenceOperators, ReadingByIndexIsConstAndReturnsWhatItIsComparedWith) {
    const Outcome o = body("Row row;\nEXPECT_EQ(row[2], 7);\n");
    const ir::Method& m = method(o, "Row", "operator[]", 1);
    EXPECT_EQ(m.ret.spelling, "int");
    EXPECT_TRUE(m.is_const);
    EXPECT_EQ(param_types(m), std::vector<std::string>{"int"});
}

TEST(InferenceOperators, WritingByIndexReturnsAReferenceAndIsNotConst) {
    const Outcome o = body("Row row;\nrow[0] = 5;\nEXPECT_EQ(row[1], 6);\n");
    const ir::Method& m = method(o, "Row", "operator[]", 1);
    EXPECT_EQ(m.ret.spelling, "int&");
    EXPECT_FALSE(m.is_const);
}

TEST(InferenceOperators, ACompoundAssignmentAlsoWritesAndTakesItsTypeFromAReader) {
    const Outcome o = body("Row row;\nrow[0] += 2;\nEXPECT_EQ(row[0], 6);\n");
    EXPECT_EQ(method(o, "Row", "operator[]", 1).ret.spelling, "int&");
}

TEST(InferenceOperators, AnIndexMayBeAString) {
    const Outcome o = body("Config cfg;\nEXPECT_EQ(cfg[\"name\"], std::string(\"x\"));\n");
    EXPECT_EQ(param_types(method(o, "Config", "operator[]", 1)), std::vector<std::string>{"const char*"});
    EXPECT_EQ(method(o, "Config", "operator[]", 1).ret.spelling, "std::string");
}

TEST(InferenceOperators, AnOverrideMayStillMakeAnOperatorNonConst) {
    core::Config config;
    core::MemberOverride over;
    over.is_const = false;
    config.overrides["Point::operator=="] = over;
    const Outcome o = body("Point a;\nPoint b;\nEXPECT_TRUE(a == b);\n", config);
    EXPECT_FALSE(method(o, "Point", "operator==", 1).is_const);
}

TEST(InferenceOperators, AClassOfOperatorsOnlyIsNotAStruct) {
    const Outcome o = body("Point a;\nPoint b;\nEXPECT_TRUE(a == b);\n");
    EXPECT_FALSE(the_class(o, "Point").is_struct);
}

// ---- enums: Color::Red --------------------------------------------------------------------------------------------------------

namespace {

const ir::Enum* find_enum(const Outcome& o, const char* name) {
    for (const ir::Enum& e : o.model.enums) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

}  // namespace

TEST(InferenceEnums, AnEnumHasTheEnumeratorsTheTestUsesAndIsScoped) {
    const Outcome o = body("Paint paint;\npaint.set(Color::Red);\npaint.set(Color::Blue);\n");
    const ir::Enum* color = find_enum(o, "Color");
    ASSERT_NE(color, nullptr);
    EXPECT_TRUE(color->is_scoped);
    EXPECT_EQ(color->enumerators, (std::vector<std::string>{"Red", "Blue"}));
    EXPECT_FALSE(color->underlying.has_value());
    EXPECT_EQ(color->where.line, 4u);
}

TEST(InferenceEnums, EveryEnumeratorSaysWhereItCameFromAndTheFirstWhyTheNameIsAnEnum) {
    const Outcome o = body("Paint paint;\npaint.set(Color::Red);\npaint.set(Color::Blue);\n");
    const ir::Enum* color = find_enum(o, "Color");
    ASSERT_NE(color, nullptr);
    ASSERT_EQ(color->enumerator_notes.size(), 2u);
    ASSERT_EQ(color->enumerator_notes[0].size(), 2u);
    EXPECT_EQ(color->enumerator_notes[0][0], "'Color' is taken for an enum because the test uses 'Color::Red' and nothing says what else it could be");
    EXPECT_EQ(color->enumerator_notes[0][1], "enumerator 'Red' inferred from Color::Red (t.cpp:4)");
    ASSERT_EQ(color->enumerator_notes[1].size(), 1u);
    EXPECT_EQ(color->enumerator_notes[1][0], "enumerator 'Blue' inferred from Color::Blue (t.cpp:5)");
}

TEST(InferenceEnums, AMethodTakesAndReturnsTheEnumByValue) {
    const Outcome o = body("Paint paint;\npaint.set(Color::Red);\nEXPECT_EQ(paint.color(), Color::Blue);\n");
    EXPECT_EQ(param_types(method(o, "Paint", "set", 1)), std::vector<std::string>{"Color"}) << "an enum is cheap to copy: no const&";
    EXPECT_EQ(method(o, "Paint", "color").ret.spelling, "Color");
    EXPECT_EQ(method(o, "Paint", "color").ret.origin, ir::Origin::Inferred);
}

TEST(InferenceEnums, AnEnumOfANamespaceKeepsItAndTheClassAroundIt) {
    const Outcome o = body("acme::Paint paint;\npaint.set(acme::Color::Red);\n");
    const ir::Enum* color = find_enum(o, "Color");
    ASSERT_NE(color, nullptr);
    EXPECT_EQ(color->ns, std::vector<std::string>{"acme"});
    EXPECT_EQ(param_types(method(o, "Paint", "set", 1)), std::vector<std::string>{"acme::Color"});
}

TEST(InferenceEnums, AVariableHoldingAnEnumeratorTypesTheArgumentToo) {
    const Outcome o = body("Color c = Color::Red;\nPaint paint;\npaint.set(c);\n");
    EXPECT_EQ(param_types(method(o, "Paint", "set", 1)), std::vector<std::string>{"Color"});
    EXPECT_NE(find_enum(o, "Color"), nullptr);
}

TEST(InferenceEnums, ADeclaredEnumIsNotGeneratedAgain) {
    const Outcome o = infer_text("enum class Color { Red, Blue };\nTEST(S, N) { Paint paint; paint.set(Color::Red); }\n");
    EXPECT_TRUE(o.model.enums.empty());
    EXPECT_EQ(param_types(method(o, "Paint", "set", 1)), std::vector<std::string>{"Color"});
}

TEST(InferenceEnums, WhatIsNotAnEnumStillNeedsAnAnnotation) {
    const Outcome o = body("Paint paint;\npaint.set(acme::kMax);\n");
    EXPECT_TRUE(o.model.enums.empty());
    bool unknown = false;
    for (const core::Diagnostic& d : o.diagnostics.sorted()) unknown = unknown || d.code == core::code::unknown_param;
    EXPECT_TRUE(unknown);
}

TEST(InferenceEnums, EveryPrefixOfATestWithEnumsIsInferredWithoutIncident) {
    const std::string text =
        "enum class Mode { Slow };\n"
        "TEST(S, N) {\n"
        "    Paint paint;\n"
        "    paint.set(Color::Red);\n"
        "    EXPECT_EQ(paint.color(), Color::Blue);\n"
        "    Color c = Color::Green;\n"
        "    Color d(Color::Red);\n"
        "    paint.mode(Mode::Slow);\n"
        "    switch (paint.color()) { case Color::Red: break; default: break; }\n"
        "}\n";
    for (std::size_t n = 0; n <= text.size(); ++n) {
        const Outcome o = infer_text(text.substr(0, n));
        EXPECT_LE(o.model.enums.size(), 1u) << "cut at " << n;
    }
    EXPECT_EQ(infer_text(text).model.enums.size(), 1u);
}

// ---- exception classes: EXPECT_THROW(stmt, E) ---------------------------------------------------------------------------------

namespace {

core::Config throwing(const char* base = "std::runtime_error") {
    core::Config config;
    config.exception_base = base;
    return config;
}

}  // namespace

TEST(InferenceExceptions, AThrownTypeDerivesFromTheConfiguredBaseAndInheritsItsConstructors) {
    const Outcome o = body("Parser parser;\nEXPECT_THROW(parser.parse(\"x\"), ParseError);\n", throwing());
    const ir::Class& error = the_class(o, "ParseError");
    ASSERT_EQ(error.bases.size(), 1u);
    EXPECT_EQ(error.bases[0].name, "std::runtime_error");
    EXPECT_EQ(error.bases[0].access, ir::Access::Public);
    EXPECT_FALSE(error.bases[0].is_virtual);
    EXPECT_EQ(error.inherited_constructors, std::vector<std::string>{"std::runtime_error"});
    EXPECT_TRUE(error.methods.empty());
    EXPECT_FALSE(error.is_struct);
}

TEST(InferenceExceptions, TheClassSaysWhyItHasTheBaseItHas) {
    const Outcome o = body("Parser parser;\nEXPECT_THROW(parser.parse(\"x\"), ParseError);\n", throwing("acme::Error"));
    ASSERT_EQ(the_class(o, "ParseError").notes.size(), 1u);
    EXPECT_EQ(the_class(o, "ParseError").notes[0],
              "derives from 'acme::Error' because the test expects it to be thrown (t.cpp:4); exception_base says which base");
}

TEST(InferenceExceptions, ThatIsAllItHasWhenTheTestSaysNothingElseOfIt) {
    const Outcome o = body("Parser parser;\nEXPECT_THROW(parser.parse(\"x\"), ParseError);\n", throwing());
    EXPECT_TRUE(the_class(o, "ParseError").methods.empty());
    EXPECT_TRUE(the_class(o, "ParseError").fields.empty());
}

TEST(InferenceExceptions, AClassTheTestUsesAsWellKeepsItsMembers) {
    const Outcome o = body("Parser parser;\nParseError error(3);\nEXPECT_EQ(error.code(), 3);\nEXPECT_THROW(parser.parse(\"x\"), ParseError);\n", throwing());
    const ir::Class& error = the_class(o, "ParseError");
    EXPECT_EQ(error.bases.size(), 1u);
    EXPECT_NE(find_method(error, "code", 0), nullptr);
    EXPECT_EQ(constructors(error).size(), 1u);
}

TEST(InferenceExceptions, WithoutAConfiguredBaseThereIsNoSuchClass) {
    const Outcome o = body("Parser parser;\nEXPECT_THROW(parser.parse(\"x\"), ParseError);\n");
    EXPECT_EQ(o.model.classes.size(), 1u);
    for (const ir::Class& c : o.model.classes) {
        EXPECT_TRUE(c.bases.empty());
        EXPECT_TRUE(c.notes.empty());
    }
}

TEST(InferenceExceptions, ANormalClassHasNoBaseAndNoNote) {
    const Outcome o = body("Parser parser;\nEXPECT_EQ(parser.size(), 3u);\n", throwing());
    EXPECT_TRUE(the_class(o, "Parser").bases.empty());
    EXPECT_TRUE(the_class(o, "Parser").inherited_constructors.empty());
    EXPECT_TRUE(the_class(o, "Parser").notes.empty());
}

}  // namespace
