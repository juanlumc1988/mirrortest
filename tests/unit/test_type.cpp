// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <string>

#include "mirrortest/ir/type.h"

using mt::ir::make_type;
using mt::ir::normalize_spelling;
using mt::ir::qualified_name;
using mt::ir::RefKind;

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
