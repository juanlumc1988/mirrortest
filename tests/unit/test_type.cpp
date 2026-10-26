// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <string>

#include "mirrortest/ir/type.h"

using mt::ir::make_type;
using mt::ir::normalize_spelling;
using mt::ir::qualified_name;
using mt::ir::RefKind;
using mt::ir::Type;
using mt::ir::type_from_spelling;

TEST(NormalizeSpelling, CollapsesWhitespaceAroundQualifiers) {
    EXPECT_EQ(normalize_spelling("const  std::string &"), "const std::string&");
    EXPECT_EQ(normalize_spelling("  int  "), "int");
    EXPECT_EQ(normalize_spelling("std :: size_t"), "std::size_t");
    EXPECT_EQ(normalize_spelling("char * *"), "char**");
    EXPECT_EQ(normalize_spelling("Foo &&"), "Foo&&");
}

TEST(NormalizeSpelling, KeepsSpacesThatSeparateWords) {
    EXPECT_EQ(normalize_spelling("unsigned   long\tlong"), "unsigned long long");
    EXPECT_EQ(normalize_spelling("int * const"), "int* const");
    EXPECT_EQ(normalize_spelling("const ::ns::T&"), "const ::ns::T&");
    EXPECT_EQ(normalize_spelling("::testing::Test"), "::testing::Test");
}

TEST(NormalizeSpelling, CanonicalTemplateArguments) {
    EXPECT_EQ(normalize_spelling("std::map< int,std::string >"), "std::map<int, std::string>");
    EXPECT_EQ(normalize_spelling("std::vector<std::vector<int> >"), "std::vector<std::vector<int>>");
    EXPECT_EQ(normalize_spelling("std::array<int,3>"), "std::array<int, 3>");
}

TEST(NormalizeSpelling, NeverReordersTokens) {
    // East-const stays east-const: only whitespace is the tool's business.
    EXPECT_EQ(normalize_spelling("T const &"), "T const&");
}

TEST(NormalizeSpelling, IsIdempotent) {
    const char* const inputs[] = {
        "const  std::string &", "std::map< int,std::string >", "int * const",
        "void (*)(int, double)", "std::function<void (int)>", "unsigned", "",
    };
    for (const char* in : inputs) {
        const std::string once = normalize_spelling(in);
        EXPECT_EQ(normalize_spelling(once), once) << "input: '" << in << "'";
    }
}

TEST(MakeType, BuildsCanonicalSpellings) {
    EXPECT_EQ(make_type("int").spelling, "int");
    EXPECT_EQ(make_type("std::string", true, RefKind::LValue).spelling, "const std::string&");
    EXPECT_EQ(make_type("char", true, RefKind::None, 1).spelling, "const char*");
    EXPECT_EQ(make_type("Foo", false, RefKind::RValue).spelling, "Foo&&");
    EXPECT_EQ(make_type("int", false, RefKind::None, 2).spelling, "int**");
}

TEST(MakeType, DecomposedFieldsMatchTheSpelling) {
    const mt::ir::Type t = make_type("  std::vector< int > ", true, RefKind::LValue);
    EXPECT_EQ(t.base, "std::vector<int>");
    EXPECT_TRUE(t.is_const);
    EXPECT_EQ(t.ref, RefKind::LValue);
    EXPECT_EQ(t.pointer_depth, 0u);
    EXPECT_EQ(t.spelling, "const std::vector<int>&");
    EXPECT_FALSE(t.is_void());
    EXPECT_TRUE(make_type("void").is_void());
}

TEST(QualifiedName, JoinsNamespaces) {
    EXPECT_EQ(qualified_name({}, "Foo"), "Foo");
    EXPECT_EQ(qualified_name({"acme"}, "Foo"), "acme::Foo");
    EXPECT_EQ(qualified_name({"acme", "core"}, "Widget"), "acme::core::Widget");
}

// ---- type_from_spelling ---------------------------------------------------------------------------------------

namespace {

struct SpellingCase {
    const char* text;
    const char* spelling;
    const char* base;
    bool        is_const;
    RefKind     ref;
    unsigned    depth;
};

}  // namespace

TEST(TypeFromSpelling, ReadsTheShapeFromTheEnd) {
    const SpellingCase cases[] = {
        {"int", "int", "int", false, RefKind::None, 0},
        {"unsigned   int", "unsigned int", "unsigned int", false, RefKind::None, 0},
        {"const Item*", "const Item*", "Item", true, RefKind::None, 1},
        {"Item const*", "Item const*", "Item", true, RefKind::None, 1},
        {"Item const &", "Item const&", "Item", true, RefKind::LValue, 0},
        {"const std::string &", "const std::string&", "std::string", true, RefKind::LValue, 0},
        {"Item&&", "Item&&", "Item", false, RefKind::RValue, 0},
        {"std::vector< int >", "std::vector<int>", "std::vector<int>", false, RefKind::None, 0},
        {"std::map<int, std::string>*", "std::map<int, std::string>*", "std::map<int, std::string>", false, RefKind::None, 1},
        {"char* const", "char* const", "char", false, RefKind::None, 1},
        {"const char* const*", "const char* const*", "char", true, RefKind::None, 2},
        {"Item * *", "Item**", "Item", false, RefKind::None, 2},
        {"std::unique_ptr<const Item>", "std::unique_ptr<const Item>", "std::unique_ptr<const Item>", false, RefKind::None, 0},
        {"constant", "constant", "constant", false, RefKind::None, 0},
        {"const_iterator", "const_iterator", "const_iterator", false, RefKind::None, 0},
    };
    for (const SpellingCase& c : cases) {
        const Type t = type_from_spelling(c.text);
        EXPECT_EQ(t.spelling, c.spelling) << c.text;
        EXPECT_EQ(t.base, c.base) << c.text;
        EXPECT_EQ(t.is_const, c.is_const) << c.text;
        EXPECT_EQ(t.ref, c.ref) << c.text;
        EXPECT_EQ(t.pointer_depth, c.depth) << c.text;
    }
}

TEST(TypeFromSpelling, FunctionPointersAndArraysAreKeptWhole) {
    for (const char* text : {"void (*)(int)", "int[3]", "const char* names[]"}) {
        const Type t = type_from_spelling(text);
        EXPECT_EQ(t.base, t.spelling) << text;
        EXPECT_EQ(t.pointer_depth, 0u) << text;
        EXPECT_FALSE(t.is_const) << text;
    }
}

TEST(TypeFromSpelling, AgreesWithMakeTypeOnTheCommonShapes) {
    for (const char* text : {"int", "const std::string&", "const char*", "Item&&", "std::vector<int>*"}) {
        const Type from_text = type_from_spelling(text);
        const Type rebuilt = make_type(from_text.base, from_text.is_const, from_text.ref, from_text.pointer_depth);
        EXPECT_EQ(rebuilt.spelling, from_text.spelling) << text;
    }
}

TEST(TypeFromSpelling, EmptyAndOddInputsDoNotCrash) {
    EXPECT_TRUE(type_from_spelling("").empty());
    EXPECT_TRUE(type_from_spelling("   ").empty());
    EXPECT_EQ(type_from_spelling("const").base, "");
    EXPECT_EQ(type_from_spelling("*").pointer_depth, 1u);
    EXPECT_EQ(type_from_spelling("&&&").ref, RefKind::RValue);
}

TEST(CollapseSpaces, AnyRunOfBlanksIsOneSpaceAndTheEndsAreTrimmed) {
    EXPECT_EQ(mt::ir::collapse_spaces("  typename   T ,\tint\n N  "), "typename T , int N");
    EXPECT_EQ(mt::ir::collapse_spaces("a\r\nb"), "a b");
    EXPECT_EQ(mt::ir::collapse_spaces(""), "");
    EXPECT_EQ(mt::ir::collapse_spaces(" \t\n "), "");
    EXPECT_EQ(mt::ir::collapse_spaces("one"), "one");
}

TEST(OperatorNames, TheKeywordFollowedByAnythingButAnIdentifierCharacter) {
    for (const char* name : {"operator==", "operator ==", "operator()", "operator[]", "operator new", "operator bool", "operator<<", "operator->"}) {
        EXPECT_TRUE(mt::ir::is_operator_name(name)) << name;
    }
    for (const char* name : {"operator", "operatorX", "operator_", "operator9", "my_operator", "Operator==", "size", ""}) {
        EXPECT_FALSE(mt::ir::is_operator_name(name)) << name;
    }
}

TEST(OperatorNames, OneSpellingForEachOperator) {
    EXPECT_EQ(mt::ir::normalize_operator_name("operator =="), "operator==");
    EXPECT_EQ(mt::ir::normalize_operator_name("operator  ( )"), "operator()");
    EXPECT_EQ(mt::ir::normalize_operator_name("operator [ ]"), "operator[]");
    EXPECT_EQ(mt::ir::normalize_operator_name("operator<<"), "operator<<");
    EXPECT_EQ(mt::ir::normalize_operator_name("operator  new"), "operator new");
    EXPECT_EQ(mt::ir::normalize_operator_name("operator  new [ ]"), "operator new[]");
    EXPECT_EQ(mt::ir::normalize_operator_name("operator   bool"), "operator bool");
    EXPECT_EQ(mt::ir::normalize_operator_name("operator"), "operator") << "nothing after the keyword: left alone";
}

TEST(OperatorNames, WhatIsNotAnOperatorIsReturnedUntouched) {
    for (const char* name : {"size", "operatorX", "my  operator ==", ""}) EXPECT_EQ(mt::ir::normalize_operator_name(name), name) << name;
}
