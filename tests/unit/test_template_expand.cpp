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
