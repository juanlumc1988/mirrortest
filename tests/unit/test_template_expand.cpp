// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Class templates in, one class per configured instantiation out.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mirrortest/backend/template_expand.h"
#include "mirrortest/frontend/code_frontend.h"
#include "mirrortest/ir/type.h"

using namespace mt;
using backend::TemplateParam;

namespace {

struct Expanded {
    ir::Model         model;
    core::Diagnostics diagnostics;
};

Expanded expand(const std::string& header, std::map<std::string, std::vector<std::string>> instantiations) {
    Expanded e;
    const ir::Model model = frontend::read_header({"box.h", header}, "box.h", {}, e.diagnostics);
    core::Config config;
    config.template_instantiations = std::move(instantiations);
    e.model = backend::instantiate_templates(model, config, e.diagnostics);
    return e;
}

std::vector<std::string> class_names(const Expanded& e) {
    std::vector<std::string> out;
    for (const ir::Class& c : e.model.classes) out.push_back(ir::qualified_name(c.ns, c.name));
    return out;
}

const ir::Class& the_class(const Expanded& e, const char* name) {
    static const ir::Class none;
    for (const ir::Class& c : e.model.classes) {
        if (c.name == name) return c;
    }
    ADD_FAILURE() << "no class " << name;
    return none;
}

std::string signature(const ir::Method& m) {
    std::string out = m.ret.spelling + " " + m.name + "(";
    for (std::size_t i = 0; i < m.params.size(); ++i) out += (i == 0 ? "" : ", ") + m.params[i].type.spelling;
    return out + ")" + (m.is_const ? " const" : "");
}

std::vector<std::string> signatures(const ir::Class& c) {
    std::vector<std::string> out;
    for (const ir::Method& m : c.methods) out.push_back(signature(m));
    return out;
}

std::vector<std::string> warnings(const Expanded& e) {
    std::vector<std::string> out;
    for (const core::Diagnostic& d : e.diagnostics.all()) {
        if (d.severity == core::Severity::Warning) out.push_back(d.message);
    }
    return out;
}

using Lines = std::vector<std::string>;

}  // namespace

// ---- the parameters -----------------------------------------------------------------------------------------

TEST(TemplateParams, NamesAndDefaults) {
    std::vector<TemplateParam> p;
    ASSERT_TRUE(backend::parse_template_params("typename T, int N = 4, class A = std::allocator<T>", p));
    ASSERT_EQ(p.size(), 3u);
    EXPECT_EQ(p[0].name, "T");
    EXPECT_FALSE(p[0].has_default);
    EXPECT_EQ(p[1].name, "N");
    EXPECT_EQ(p[1].default_value, "4");
    EXPECT_EQ(p[2].name, "A");
    EXPECT_EQ(p[2].default_value, "std::allocator<T>");
}

TEST(TemplateParams, ACommaInsideBracketsDoesNotSplit) {
    std::vector<TemplateParam> p;
    ASSERT_TRUE(backend::parse_template_params("typename K, typename V = std::map<int, int>, int N = (1 + 2)", p));
    ASSERT_EQ(p.size(), 3u);
    EXPECT_EQ(p[1].default_value, "std::map<int, int>");
    EXPECT_EQ(p[2].default_value, "(1 + 2)");
}

TEST(TemplateParams, AnEqualsInsideTheDefaultIsPartOfIt) {
    std::vector<TemplateParam> p;
    ASSERT_TRUE(backend::parse_template_params("bool B = (1 == 1), bool C = (2 >= 1)", p));
    ASSERT_EQ(p.size(), 2u);
    EXPECT_EQ(p[0].default_value, "(1 == 1)");
    EXPECT_EQ(p[1].default_value, "(2 >= 1)");
}

TEST(TemplateParams, UnnamedAndTemplateTemplateParameters) {
    std::vector<TemplateParam> p;
    ASSERT_TRUE(backend::parse_template_params("typename, int = 3, template <typename> class C", p));
    ASSERT_EQ(p.size(), 3u);
    EXPECT_EQ(p[0].name, "");
    EXPECT_EQ(p[1].name, "");
    EXPECT_EQ(p[1].default_value, "3");
    EXPECT_EQ(p[2].name, "C");
}

TEST(TemplateParams, APackHasNoSingleArgument) {
    std::vector<TemplateParam> p;
    EXPECT_FALSE(backend::parse_template_params("typename... Ts", p));
    EXPECT_FALSE(backend::parse_template_params("typename T, int... Ns", p));
}

TEST(TemplateParams, SpacesAroundTheParametersAreNotPartOfThem) {
    std::vector<TemplateParam> p;
    ASSERT_TRUE(backend::parse_template_params("  typename T  ,\n  int N = 4  ", p));
    ASSERT_EQ(p.size(), 2u);
    EXPECT_EQ(p[0].name, "T");
    EXPECT_EQ(p[1].name, "N");
    EXPECT_EQ(p[1].default_value, "4");
}

TEST(TemplateParams, AnEqualsInTheTypeBeforeTheDefaultIsNotTheDefault) {
    // The usual way to write an overload set: a non-type parameter of a type that contains `<=` or `==`.
    std::vector<TemplateParam> p;
    ASSERT_TRUE(backend::parse_template_params("typename T, std::enable_if_t<(sizeof(T) <= 4), int> = 0, std::enable_if_t<N == 3, int> = 1", p));
    ASSERT_EQ(p.size(), 3u);
    EXPECT_TRUE(p[1].has_default);
    EXPECT_EQ(p[1].default_value, "0");
    EXPECT_EQ(p[1].name, "") << "the parameter is unnamed: int, behind the closing bracket, is not its name";
    EXPECT_EQ(p[2].default_value, "1");
}

TEST(TemplateParams, OperatorsInADefaultAreNotBrackets) {
    std::vector<TemplateParam> p;
    ASSERT_TRUE(backend::parse_template_params("int N = 1 << 3, int M = 2, typename T = decltype(a->b), bool B = (3 >= 2), int K = 8 >> 1, int L = 5", p));
    ASSERT_EQ(p.size(), 6u);
    EXPECT_EQ(p[0].default_value, "1 << 3");
    EXPECT_EQ(p[1].default_value, "2");
    EXPECT_EQ(p[2].default_value, "decltype(a->b)");
    EXPECT_EQ(p[3].default_value, "(3 >= 2)");
    EXPECT_EQ(p[4].default_value, "8 >> 1");
    EXPECT_EQ(p[5].default_value, "5");
}

TEST(TemplateParams, NestedAngleBracketsCloseEachOther) {
    std::vector<TemplateParam> p;
    ASSERT_TRUE(backend::parse_template_params("typename A = std::map<int, std::vector<int>>, typename B = int", p));
    ASSERT_EQ(p.size(), 2u);
    EXPECT_EQ(p[0].default_value, "std::map<int, std::vector<int>>");
    EXPECT_EQ(p[1].default_value, "int");
}

TEST(TemplateParams, WhatIsLeftInTheOutputIsReplaced) {
    std::vector<TemplateParam> p;
    ASSERT_TRUE(backend::parse_template_params("typename A, typename B", p));
    ASSERT_TRUE(backend::parse_template_params("typename C", p));
    ASSERT_EQ(p.size(), 1u);
    EXPECT_EQ(p[0].name, "C");
    ASSERT_TRUE(backend::parse_template_params("", p));
    EXPECT_TRUE(p.empty());
}

TEST(TemplateParams, NoParametersIsFine) {
    std::vector<TemplateParam> p;
    EXPECT_TRUE(backend::parse_template_params("", p));
    EXPECT_TRUE(p.empty());
    EXPECT_TRUE(backend::parse_template_params("  ", p));
}

TEST(TemplateArguments, SplitAtTheTopLevel) {
    EXPECT_EQ(backend::split_template_arguments("int"), Lines{"int"});
    EXPECT_EQ(backend::split_template_arguments("int, 8"), (Lines{"int", "8"}));
    EXPECT_EQ(backend::split_template_arguments("std::map<int, std::string>, void(*)(int, int), 3"),
              (Lines{"std::map<int, std::string>", "void(*)(int, int)", "3"}));
    EXPECT_TRUE(backend::split_template_arguments("").empty());
    EXPECT_TRUE(backend::split_template_arguments("   ").empty());
    EXPECT_EQ(backend::split_template_arguments("  int ,\t8  "), (Lines{"int", "8"})) << "spaces belong to no argument";
    EXPECT_EQ(backend::split_template_arguments("a<(b), c>, [d, e], {f, g}"), (Lines{"a<(b), c>", "[d, e]", "{f, g}"}));
    EXPECT_EQ(backend::split_template_arguments("1 << 2, (3 < 4), std::vector<std::vector<int>>, a->b"),
              (Lines{"1 << 2", "(3 < 4)", "std::vector<std::vector<int>>", "a->b"}));
    EXPECT_EQ(backend::split_template_arguments("5 > 4, 2"), (Lines{"5 > 4", "2"})) << "a > with no < before it compares";
    EXPECT_EQ(backend::split_template_arguments("a), b"), (Lines{"a)", "b"})) << "a bracket nobody opened closes nothing";
    EXPECT_EQ(backend::split_template_arguments("a}, b, c]"), (Lines{"a}", "b", "c]"}));
}

// ---- the instantiations ----------------------------------------------------------------------------------------

TEST(TemplateExpand, EveryTypeOfTheClassIsInTermsOfTheArguments) {
    const Expanded e = expand(
        "template <typename T> class Box {\n"
        "public:\n"
        "    Box();\n"
        "    explicit Box(T value);\n"
        "    T get(int i) const;\n"
        "    void put(const T& value, std::vector<T> more);\n"
        "    T* where() const;\n"
        "    T value_;\n"
        "};\n",
        {{"Box", {"int", "std::string"}}});
    EXPECT_EQ(class_names(e), (Lines{"Box<int>", "Box<std::string>"}));
    EXPECT_EQ(signatures(the_class(e, "Box<int>")),
              (Lines{" Box()", " Box(int)", "int get(int) const", "void put(const int&, std::vector<int>)", "int* where() const"}));
    const ir::Class& s = the_class(e, "Box<std::string>");
    EXPECT_EQ(s.methods[2].ret.spelling, "std::string");
    EXPECT_EQ(s.methods[3].params[0].type.spelling, "const std::string&");
    EXPECT_EQ(s.methods[3].params[1].type.spelling, "std::vector<std::string>");
    EXPECT_EQ(s.methods[4].ret.spelling, "std::string*");
    EXPECT_EQ(s.fields[0].type.spelling, "std::string");
    EXPECT_FALSE(s.template_params.has_value());
    EXPECT_EQ(s.name, "Box<std::string>");
}

TEST(TemplateExpand, TheInjectedClassNameIsTheInstantiation) {
    const Expanded e = expand(
        "template <typename T> class Box {\n"
        "public:\n"
        "    Box clone() const;\n"
        "    Box<T> same() const;\n"
        "    bool equals(const Box& other) const;\n"
        "    std::unique_ptr<Box> own() const;\n"
        "    Other::Box foreign() const;\n"
        "};\n",
        {{"Box", {"int"}}});
    const ir::Class& c = the_class(e, "Box<int>");
    EXPECT_EQ(c.methods[0].ret.spelling, "Box<int>");
    EXPECT_EQ(c.methods[1].ret.spelling, "Box<int>");
    EXPECT_EQ(c.methods[2].params[0].type.spelling, "const Box<int>&");
    EXPECT_EQ(c.methods[3].ret.spelling, "std::unique_ptr<Box<int>>");
    EXPECT_EQ(c.methods[4].ret.spelling, "Other::Box") << "a name after :: is somebody else's";
}

TEST(TemplateExpand, DefaultsFillTheArgumentsNotGiven) {
    const Expanded e = expand(
        "template <typename T, int N = 4, typename A = std::allocator<T>> class Box {\n"
        "public:\n"
        "    std::array<T, N> all() const;\n"
        "    A allocator() const;\n"
        "};\n",
        {{"Box", {"int", "std::string, 8", "double, 2, MyAlloc"}}});
    EXPECT_EQ(class_names(e), (Lines{"Box<int>", "Box<std::string, 8>", "Box<double, 2, MyAlloc>"}));
    EXPECT_EQ(the_class(e, "Box<int>").methods[0].ret.spelling, "std::array<int, 4>");
    EXPECT_EQ(the_class(e, "Box<int>").methods[1].ret.spelling, "std::allocator<int>") << "a default may use the earlier parameters";
    EXPECT_EQ(the_class(e, "Box<std::string, 8>").methods[0].ret.spelling, "std::array<std::string, 8>");
    EXPECT_EQ(the_class(e, "Box<std::string, 8>").methods[1].ret.spelling, "std::allocator<std::string>");
    EXPECT_EQ(the_class(e, "Box<double, 2, MyAlloc>").methods[1].ret.spelling, "MyAlloc");
}

TEST(TemplateExpand, ANumberIsNotAName) {
    const Expanded e = expand(
        "template <typename T, int u = 3> class Box {\n"
        "public:\n"
        "    std::array<T, 3u> a() const;\n"
        "    std::array<T, 0x1F> b() const;\n"
        "};\n",
        {{"Box", {"int, 5"}}});
    EXPECT_EQ(the_class(e, "Box<int, 5>").methods[0].ret.spelling, "std::array<int, 3u>") << "u in 3u is part of the number";
    EXPECT_EQ(the_class(e, "Box<int, 5>").methods[1].ret.spelling, "std::array<int, 0x1F>");
}

TEST(TemplateExpand, BasesAndDefaultArgumentsToo) {
    const Expanded e = expand(
        "template <typename T> class Box : public Base<T> {\n"
        "public:\n"
        "    void fill(T value = T());\n"
        "};\n",
        {{"Box", {"int"}}});
    const ir::Class& c = the_class(e, "Box<int>");
    ASSERT_EQ(c.bases.size(), 1u);
    EXPECT_EQ(c.bases[0].name, "Base<int>");
    ASSERT_TRUE(c.methods[0].params[0].default_value.has_value());
    EXPECT_EQ(*c.methods[0].params[0].default_value, "int()");
}

TEST(TemplateExpand, TheInstantiationsStandWhereTheTemplateStood) {
    const Expanded e = expand(
        "class Before {};\n"
        "namespace acme { template <typename T> class Box { public: T get() const; }; }\n"
        "class After {};\n",
        {{"acme::Box", {"int", "char"}}});
    EXPECT_EQ(class_names(e), (Lines{"Before", "acme::Box<int>", "acme::Box<char>", "After"}));
    EXPECT_EQ(the_class(e, "Box<int>").ns, std::vector<std::string>{"acme"});
}

TEST(TemplateExpand, WhatTheConfigurationDoesNotNameIsAsItWas) {
    const Expanded e = expand(
        "template <typename T> class Box { public: T get() const; };\n"
        "template <typename T> class Other { public: T get() const; };\n"
        "class Plain { public: int get() const; };\n",
        {{"Box", {"int"}}, {"Plain", {"int"}}, {"Missing", {"int"}}});
    EXPECT_EQ(class_names(e), (Lines{"Box<int>", "Other", "Plain"}));
    EXPECT_TRUE(the_class(e, "Other").template_params.has_value());
    EXPECT_TRUE(warnings(e).empty());
}

TEST(TemplateExpand, TheQualifiedNameIsTheKey) {
    const Expanded e = expand(
        "namespace a { template <typename T> class Box { public: T get() const; }; }\n"
        "namespace b { template <typename T> class Box { public: T get() const; }; }\n",
        {{"b::Box", {"int"}}});
    EXPECT_EQ(class_names(e), (Lines{"a::Box", "b::Box<int>"}));
}

TEST(TemplateExpand, TooManyArgumentsIsAWarningAndNoInstantiation) {
    const Expanded e = expand("template <typename T> class Box { public: T get() const; };\n", {{"Box", {"int, 3", "char"}}});
    EXPECT_EQ(class_names(e), (Lines{"Box<char>"}));
    EXPECT_EQ(warnings(e), Lines{"template_instantiations: 'Box<int, 3>' gives 2 arguments and the template has 1"});
}

TEST(TemplateExpand, TooFewWithoutADefaultIsAWarning) {
    const Expanded e = expand("template <typename K, typename V> class Map { public: V get(K k) const; };\n", {{"Map", {"int", "int, char"}}});
    EXPECT_EQ(class_names(e), (Lines{"Map<int, char>"}));
    EXPECT_EQ(warnings(e), Lines{"template_instantiations: 'Map<int>' gives 1 arguments and parameter 2 of the template has no default"});
}

TEST(TemplateExpand, APackIsAWarningAndTheTemplateStaysATemplate) {
    const Expanded e = expand("template <typename... Ts> class Tuple { public: int size() const; };\n", {{"Tuple", {"int"}}});
    EXPECT_EQ(class_names(e), (Lines{"Tuple"}));
    EXPECT_TRUE(the_class(e, "Tuple").template_params.has_value());
    ASSERT_EQ(warnings(e).size(), 1u);
    EXPECT_NE(warnings(e)[0].find("parameter pack"), std::string::npos);
}

TEST(TemplateExpand, WhenNothingCanBeMadeTheTemplateStays) {
    const Expanded e = expand("template <typename T> class Box { public: T get() const; };\n", {{"Box", {"int, 3"}}});
    EXPECT_EQ(class_names(e), (Lines{"Box"}));
    EXPECT_TRUE(the_class(e, "Box").template_params.has_value());
}

TEST(TemplateExpand, AnEmptyArgumentListIsAllDefaults) {
    const Expanded e = expand("template <typename T = int> class Box { public: T get() const; };\n", {{"Box", {""}}});
    EXPECT_EQ(class_names(e), (Lines{"Box<>"}));
    EXPECT_EQ(the_class(e, "Box<>").methods[0].ret.spelling, "int");
}

TEST(TemplateExpand, AnUnnamedParameterTakesItsArgumentAndSubstitutesNothing) {
    const Expanded e = expand("template <typename, int N> class Box { public: int get() const; std::array<int, N> a() const; };\n", {{"Box", {"char, 3"}}});
    EXPECT_EQ(the_class(e, "Box<char, 3>").methods[1].ret.spelling, "std::array<int, 3>");
}

TEST(TemplateExpand, TheArgumentsAreNormalisedInTheName) {
    const Expanded e = expand("template <typename T> class Box { public: T get() const; };\n", {{"Box", {"std::map< int,int >"}}});
    EXPECT_EQ(class_names(e), (Lines{"Box<std::map<int, int>>"}));
}

TEST(TemplateExpand, ASubstitutedTypeKeepsWhereItCameFromAndWhy) {
    core::Diagnostics d;
    ir::Model model = frontend::read_header({"box.h", "template <typename T> class Box {\npublic:\n    T get(T value) const;\n};\n"}, "box.h", {}, d);
    ir::Method& get = model.classes[0].methods[0];
    get.ret.origin = ir::Origin::Inferred;
    get.ret.reason = "because I said so";
    get.params[0].type.reason = "and so";
    core::Config config;
    config.template_instantiations = {{"Box", {"int"}}};
    const ir::Model made = backend::instantiate_templates(model, config, d);
    ASSERT_EQ(made.classes.size(), 1u);
    const ir::Method& m = made.classes[0].methods[0];
    EXPECT_EQ(m.ret.spelling, "int");
    EXPECT_EQ(m.ret.origin, ir::Origin::Inferred);
    EXPECT_EQ(m.ret.reason, "because I said so");
    EXPECT_EQ(m.ret.where.line, 3u);
    EXPECT_EQ(m.params[0].type.reason, "and so");
    EXPECT_EQ(m.params[0].type.where.line, 3u);
}

TEST(TemplateExpand, ATypeThatMentionsNothingIsNotTouched) {
    const Expanded e = expand("template <typename T> class Box { public: Box(); int size() const; };\n", {{"Box", {"int"}}});
    const ir::Class& c = the_class(e, "Box<int>");
    EXPECT_EQ(c.methods[0].ret.spelling, "");
    EXPECT_EQ(c.methods[1].ret.spelling, "int");
}

TEST(TemplateExpand, TheArgumentsAndTheDefaultsAreNormalisedBeforeTheyAreSubstituted) {
    const Expanded e = expand("template <typename T, typename A = std::allocator< T >> class Box { public: A alloc() const; T get() const; };\n",
                              {{"Box", {"std::map< int , int >"}}});
    const ir::Class& c = the_class(e, "Box<std::map<int, int>>");
    EXPECT_EQ(c.methods[0].ret.spelling, "std::allocator<std::map<int, int>>");
    EXPECT_EQ(c.methods[1].ret.spelling, "std::map<int, int>");
}

TEST(TemplateExpand, ANumberWithADotOrAQuoteIsNotMistakenForAName) {
    const Expanded e = expand("template <typename f, typename x> class Box { public: std::array<f, 1'000> a(float s = 1.5f, double t = 2.0e3); };\n",
                              {{"Box", {"int, char"}}});
    const ir::Method& a = the_class(e, "Box<int, char>").methods[0];
    EXPECT_EQ(a.ret.spelling, "std::array<int, 1'000>");
    EXPECT_EQ(a.params[0].default_value.value_or(""), "1.5f");
    EXPECT_EQ(a.params[1].default_value.value_or(""), "2.0e3");
}

TEST(TemplateExpand, NothingToInstantiateLeavesTheModelAlone) {
    const Expanded e = expand("class Plain { public: int get() const; };\n", {});
    EXPECT_EQ(class_names(e), Lines{"Plain"});
    EXPECT_TRUE(e.model.classes[0].methods.size() == 1u);
}

TEST(TemplateExpand, TheOtherPartsOfTheModelAreKept) {
    const Expanded e = expand(
        "#include <vector>\n"
        "enum class E { A };\n"
        "int free_function(int);\n"
        "template <typename T> class Box { public: T get() const; };\n",
        {{"Box", {"int"}}});
    EXPECT_EQ(e.model.enums.size(), 1u);
    EXPECT_EQ(e.model.functions.size(), 1u);
    EXPECT_EQ(e.model.includes.size(), 1u);
}

// ---- function templates (design 9.7) ---------------------------------------------------------------------------

namespace {

std::vector<std::string> function_texts(const Expanded& e) {
    std::vector<std::string> out;
    for (const ir::Function& f : e.model.functions) {
        std::string text = f.ret.spelling + " " + ir::qualified_name(f.ns, f.name) + (f.template_args.empty() ? "" : "<" + f.template_args + ">") + "(";
        for (std::size_t i = 0; i < f.params.size(); ++i) {
            text += (i == 0 ? "" : ", ") + f.params[i].type.spelling;
            if (f.params[i].default_value) text += " = " + *f.params[i].default_value;
        }
        out.push_back(text + ")" + (f.template_params ? " [template]" : ""));
    }
    return out;
}

}  // namespace

TEST(TemplateExpandFunctions, AFunctionTemplateBecomesOneFunctionForEachEntryWhereItStood) {
    const Expanded e = expand(
        "namespace acme {\nint before(int);\ntemplate <typename T> T clamp_to(T lo, const T& hi, T* out);\nint after(int);\n}\n",
        {{"acme::clamp_to", {"int", "std::string"}}});
    EXPECT_EQ(function_texts(e), (Lines{"int acme::before(int)", "int acme::clamp_to<int>(int, const int&, int*)",
                                        "std::string acme::clamp_to<std::string>(std::string, const std::string&, std::string*)",
                                        "int acme::after(int)"}));
    EXPECT_TRUE(warnings(e).empty());
    EXPECT_EQ(e.model.functions[1].name, "clamp_to") << "the name is the function's: the arguments are in template_args";
}

TEST(TemplateExpandFunctions, DefaultsFillTheArgumentsNotGivenAndAreSubstitutedInDefaultArguments) {
    const Expanded e = expand("template <typename T, int N = 4> T scaled(T value, int times = N) noexcept;\n",
                              {{"scaled", {"long", "int, 8"}}});
    EXPECT_EQ(function_texts(e), (Lines{"long scaled<long>(long, int = 4)", "int scaled<int, 8>(int, int = 8)"}));
    EXPECT_TRUE(e.model.functions[0].is_noexcept);
    EXPECT_TRUE(e.model.functions[1].is_noexcept);
}

TEST(TemplateExpandFunctions, TheArgumentsAreNormalisedAndOnlyTheNamedTemplateIsTouched) {
    const Expanded e = expand("template <typename K, typename V> V lookup(const K& key, V fallback);\n"
                              "template <typename T> T other(T v);\nint plain(int v);\n",
                              {{"lookup", {"std::map<int,  int> , int"}}, {"plain", {"int"}}});
    EXPECT_EQ(function_texts(e), (Lines{"int lookup<std::map<int, int>, int>(const std::map<int, int>&, int)", "T other(T) [template]",
                                        "int plain(int)"}));
}

TEST(TemplateExpandFunctions, ATemplateThatIsNotNamedOrCannotBeMadeStaysATemplate) {
    const Expanded none = expand("template <typename T> T f(T v);\n", {});
    EXPECT_EQ(function_texts(none), Lines{"T f(T) [template]"});
    EXPECT_TRUE(warnings(none).empty());

    const Expanded many = expand("template <typename T> T f(T v);\n", {{"f", {"int, 3"}}});
    EXPECT_EQ(function_texts(many), Lines{"T f(T) [template]"});
    ASSERT_EQ(warnings(many).size(), 1u);
    EXPECT_EQ(warnings(many)[0], "template_instantiations: 'f<int, 3>' gives 2 arguments and the template has 1");

    const Expanded few = expand("template <typename T, typename U> T f(T a, U b);\n", {{"f", {"int"}}});
    EXPECT_EQ(function_texts(few), Lines{"T f(T, U) [template]"});
    ASSERT_EQ(warnings(few).size(), 1u);
    EXPECT_NE(warnings(few)[0].find("parameter 2 of the template has no default"), std::string::npos);
}

TEST(TemplateExpandFunctions, AParameterPackIsAWarningAndTheTemplateStays) {
    const Expanded e = expand("template <typename... Ts> int count_all(Ts... values);\n", {{"count_all", {"int"}}});
    EXPECT_EQ(function_texts(e), Lines{"int count_all(Ts...) [template]"});
    ASSERT_EQ(warnings(e).size(), 1u);
    EXPECT_EQ(warnings(e)[0], "template_instantiations: 'count_all' has a parameter pack, which a test cannot instantiate; the function is not tested");
}

TEST(TemplateExpandFunctions, AnOverloadSetIsInstantiatedTemplateByTemplateAndAMisfitIsAWarningForThatOne) {
    const Expanded e = expand("template <typename T> void put(T v);\ntemplate <typename T, typename U> void put(T a, U b);\n"
                              "template <typename T, typename U = int> void put(T a, T b, U c);\n",
                              {{"put", {"char"}}});
    EXPECT_EQ(function_texts(e), (Lines{"void put<char>(char)", "void put(T, U) [template]", "void put<char>(char, char, int)"}));
    ASSERT_EQ(warnings(e).size(), 1u);
}

TEST(TemplateExpandFunctions, ASubstitutedTypeKeepsWhereItCameFromAndWhy) {
    core::Diagnostics d;
    ir::Model model = frontend::read_header({"t.h", "template <typename T> T f(T v);\n"}, "t.h", {}, d);
    model.functions[0].ret.origin = ir::Origin::Inferred;
    model.functions[0].ret.reason = "because I said so";
    core::Config config;
    config.template_instantiations = {{"f", {"int"}}};
    const ir::Model out = backend::instantiate_templates(model, config, d);
    ASSERT_EQ(out.functions.size(), 1u);
    EXPECT_EQ(out.functions[0].ret.spelling, "int");
    EXPECT_EQ(out.functions[0].ret.origin, ir::Origin::Inferred);
    EXPECT_EQ(out.functions[0].ret.reason, "because I said so");
    EXPECT_EQ(out.functions[0].ret.where.line, model.functions[0].ret.where.line);
    EXPECT_EQ(out.functions[0].where.line, model.functions[0].where.line);
}

TEST(TemplateExpandFunctions, ATypeThatDependsOnAParameterIsSubstitutedAsText) {
    const Expanded e = expand("template <typename C> typename C::value_type first(const C& c);\n", {{"first", {"std::vector<int>"}}});
    EXPECT_EQ(function_texts(e), Lines{"typename std::vector<int>::value_type first<std::vector<int>>(const std::vector<int>&)"});
}
