// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "mirrortest/core/naming.h"

using namespace mt::core;

// ---- identifiers --------------------------------------------------------------------

TEST(Identifier, AcceptsAndRejects) {
    for (const char* ok : {"a", "_", "_a1", "Foo", "foo_bar2", "__x"}) {
        EXPECT_TRUE(is_identifier(ok)) << ok;
    }
    for (const char* bad : {"", "1a", "a-b", "a b", "a::b", "a.b", "é"}) {
        EXPECT_FALSE(is_identifier(bad)) << bad;
    }
}

TEST(QualifiedName, CountsParts) {
    EXPECT_TRUE(is_qualified_name("Foo", 1));
    EXPECT_FALSE(is_qualified_name("Foo", 2));
    EXPECT_TRUE(is_qualified_name("a::Foo", 2));
    EXPECT_TRUE(is_qualified_name("a::b::Foo::bar", 2));
    EXPECT_FALSE(is_qualified_name("::Foo", 1));
    EXPECT_FALSE(is_qualified_name("Foo::", 1));
    EXPECT_FALSE(is_qualified_name("a:::b", 1));
    EXPECT_FALSE(is_qualified_name("", 1));
    EXPECT_FALSE(is_qualified_name("a::1b", 1));
}

// ---- snake_case ---------------------------------------------------------------------

TEST(SnakeCase, PlainCamelAndPascal) {
    EXPECT_EQ(snake_case("foo"), "foo");
    EXPECT_EQ(snake_case("Foo"), "foo");
    EXPECT_EQ(snake_case("FooBar"), "foo_bar");
    EXPECT_EQ(snake_case("fooBar"), "foo_bar");
    EXPECT_EQ(snake_case("FooBarBaz"), "foo_bar_baz");
    EXPECT_EQ(snake_case("A"), "a");
}

TEST(SnakeCase, Acronyms) {
    EXPECT_EQ(snake_case("HTTPServer"), "http_server");
    EXPECT_EQ(snake_case("IOStream"), "io_stream");
    EXPECT_EQ(snake_case("XMLHttpRequest"), "xml_http_request");
    EXPECT_EQ(snake_case("getHTTPResponse"), "get_http_response");
    EXPECT_EQ(snake_case("getHTTP"), "get_http");
    EXPECT_EQ(snake_case("ABc"), "a_bc");
    EXPECT_EQ(snake_case("AB"), "ab");
    EXPECT_EQ(snake_case("FOO"), "foo");
    EXPECT_EQ(snake_case("CAN"), "can");
}

TEST(SnakeCase, DigitsStayWithTheWordBeforeThem) {
    EXPECT_EQ(snake_case("Base64Encoder"), "base64_encoder");
    EXPECT_EQ(snake_case("UTF8String"), "utf8_string");
    EXPECT_EQ(snake_case("Vec3"), "vec3");
    EXPECT_EQ(snake_case("Vec3D"), "vec3_d");
    EXPECT_EQ(snake_case("Http2Server"), "http2_server");
    EXPECT_EQ(snake_case("3dFoo"), "3d_foo");
    EXPECT_EQ(snake_case("uint32_t"), "uint32_t");
}

TEST(SnakeCase, SeparatorsAreCollapsedAndTrimmed) {
    EXPECT_EQ(snake_case("foo_bar"), "foo_bar");
    EXPECT_EQ(snake_case("Foo_Bar"), "foo_bar");
    EXPECT_EQ(snake_case("FOO_BAR"), "foo_bar");
    EXPECT_EQ(snake_case("_Foo"), "foo");
    EXPECT_EQ(snake_case("__Foo__Bar__"), "foo_bar");
    EXPECT_EQ(snake_case("foo-bar"), "foo_bar");
    EXPECT_EQ(snake_case("foo bar"), "foo_bar");
    EXPECT_EQ(snake_case("acme::Foo"), "acme_foo");
    EXPECT_EQ(snake_case("Box<int>"), "box_int");
}

TEST(SnakeCase, NothingLeftGivesAPlaceholder) {
    EXPECT_EQ(snake_case(""), "unnamed");
    EXPECT_EQ(snake_case("_"), "unnamed");
    EXPECT_EQ(snake_case("___"), "unnamed");
    EXPECT_EQ(snake_case("<>"), "unnamed");
}

TEST(SnakeCase, NonAsciiBytesAreLettersAndNeverSplit) {
    EXPECT_EQ(snake_case("Caf\xC3\xA9" "Au"), "caf\xC3\xA9" "_au");   // split: \xA9A would be one escape
    EXPECT_EQ(snake_case("\xC3\xA9tat"), "\xC3\xA9tat");
}

TEST(SnakeCase, IsIdempotent) {
    for (const char* in : {"HTTPServer", "Vec3D", "_Foo__Bar_", "getHTTPResponse", "A", "UTF8String"}) {
        const std::string once = snake_case(in);
        EXPECT_EQ(snake_case(once), once) << in;
    }
}

// ---- file names and matching ---------------------------------------------------------

TEST(FileStem, StripsDirectoriesAndTheLastExtension) {
    EXPECT_EQ(file_stem("foo.h"), "foo");
    EXPECT_EQ(file_stem("acme/foo_bar.h"), "foo_bar");
    EXPECT_EQ(file_stem("a/b/c/Foo.hpp"), "Foo");
    EXPECT_EQ(file_stem("C:\\x\\Foo.hpp"), "Foo");
    EXPECT_EQ(file_stem("a.b.h"), "a.b");
    EXPECT_EQ(file_stem("noext"), "noext");
    EXPECT_EQ(file_stem(".hidden"), ".hidden");
    EXPECT_EQ(file_stem("dir/"), "");
    EXPECT_EQ(file_stem(""), "");
}

TEST(MatchKey, PairsClassesWithHeaders) {
    EXPECT_EQ(match_key("FooBar"), "foobar");
    EXPECT_EQ(match_key("foo_bar"), "foobar");
    EXPECT_EQ(match_key("foo-bar"), "foobar");
    EXPECT_EQ(match_key("FOO_BAR"), "foobar");
    EXPECT_EQ(match_key(file_stem("acme/foo_bar.h")), match_key("FooBar"));
    EXPECT_NE(match_key("Foo"), match_key("FooBar"));
    EXPECT_EQ(match_key("Vec3"), "vec3");
}

// ---- include guards -----------------------------------------------------------------------

TEST(IncludeGuard, FromThePathItIsIncludedBy) {
    EXPECT_EQ(include_guard("foo.h"), "FOO_H");
    EXPECT_EQ(include_guard("acme/foo.h"), "ACME_FOO_H");
    EXPECT_EQ(include_guard("acme/core/foo_bar.hpp"), "ACME_CORE_FOO_BAR_HPP");
    EXPECT_EQ(include_guard("acme/FooBar.h"), "ACME_FOOBAR_H");
}

TEST(IncludeGuard, NeverStartsOrContainsAReservedUnderscorePattern) {
    EXPECT_EQ(include_guard("../include/foo.h"), "INCLUDE_FOO_H");
    EXPECT_EQ(include_guard("./foo.h"), "FOO_H");
    EXPECT_EQ(include_guard("a//b__c.h"), "A_B_C_H");
    EXPECT_EQ(include_guard("_foo.h"), "FOO_H");
    for (const char* in : {"../a.h", "__x__.h", "a b/c-d.h", "x/_/y.h"}) {
        const std::string g = include_guard(in);
        EXPECT_EQ(g.find("__"), std::string::npos) << in;
        EXPECT_NE(g.front(), '_') << in;
        EXPECT_TRUE(is_identifier(g)) << in;
    }
}

TEST(IncludeGuard, ALeadingDigitGetsAPrefix) {
    EXPECT_EQ(include_guard("3d/vec.h"), "H_3D_VEC_H");
    EXPECT_TRUE(is_identifier(include_guard("3d/vec.h")));
}

TEST(IncludeGuard, NothingUsableStillGivesAnIdentifier) {
    EXPECT_EQ(include_guard(""), "MT_INCLUDE_GUARD");
    EXPECT_EQ(include_guard("../"), "MT_INCLUDE_GUARD");
}

// ---- patterns ------------------------------------------------------------------------------

TEST(ExpandPattern, ReplacesBothPlaceholders) {
    EXPECT_EQ(expand_pattern("{stem}.h", "foo_bar", "FooBar"), "foo_bar.h");
    EXPECT_EQ(expand_pattern("tst_{stem}.cpp", "foo", "Foo"), "tst_foo.cpp");
    EXPECT_EQ(expand_pattern("{Class}Test", "foo", "Foo"), "FooTest");
    EXPECT_EQ(expand_pattern("Test{Class}_{stem}", "foo", "Foo"), "TestFoo_foo");
    EXPECT_EQ(expand_pattern("{stem}{stem}", "a", "A"), "aa");
}

TEST(ExpandPattern, DoesNotRescanWhatItSubstituted) {
    EXPECT_EQ(expand_pattern("{stem}.h", "{Class}", "X"), "{Class}.h");
}

TEST(ExpandPattern, CopiesEverythingElseVerbatim) {
    EXPECT_EQ(expand_pattern("plain.h", "foo", "Foo"), "plain.h");
    EXPECT_EQ(expand_pattern("{other}", "foo", "Foo"), "{other}");
    EXPECT_EQ(expand_pattern("{stem", "foo", "Foo"), "{stem");
    EXPECT_EQ(expand_pattern("", "foo", "Foo"), "");
}

// ---- test case names --------------------------------------------------------------------------

TEST(TestBaseName, OrdinaryMethodsAreUnchanged) {
    EXPECT_EQ(test_base_name("size"), "size");
    EXPECT_EQ(test_base_name("setValue"), "setValue");
    EXPECT_EQ(test_base_name("operatorX"), "operatorX");
    EXPECT_EQ(test_base_name("operator_"), "operator_");
    EXPECT_EQ(test_base_name("operator"), "operator");
}

TEST(TestBaseName, EveryOverloadableSymbolHasAName) {
    const struct { const char* op; const char* name; } cases[] = {
        {"==", "Eq"}, {"!=", "Ne"}, {"<", "Lt"}, {"<=", "Le"}, {">", "Gt"}, {">=", "Ge"},
        {"<=>", "Spaceship"}, {"+", "Plus"}, {"-", "Minus"}, {"*", "Mul"}, {"/", "Div"},
        {"%", "Mod"}, {"++", "Inc"}, {"--", "Dec"}, {"+=", "AddAssign"}, {"-=", "SubAssign"},
        {"*=", "MulAssign"}, {"/=", "DivAssign"}, {"%=", "ModAssign"}, {"&=", "AndAssign"},
        {"|=", "OrAssign"}, {"^=", "XorAssign"}, {"<<=", "ShlAssign"}, {">>=", "ShrAssign"},
        {"&", "BitAnd"}, {"|", "BitOr"}, {"^", "BitXor"}, {"~", "BitNot"}, {"<<", "Shl"},
        {">>", "Shr"}, {"!", "Not"}, {"&&", "And"}, {"||", "Or"}, {"=", "Assign"},
        {"[]", "Index"}, {"()", "Call"}, {"->", "Arrow"}, {"->*", "ArrowStar"}, {",", "Comma"},
    };
    std::set<std::string> seen;
    for (const auto& c : cases) {
        const std::string name = test_base_name(std::string("operator") + c.op);
        EXPECT_EQ(name, std::string("Operator") + c.name) << c.op;
        EXPECT_TRUE(is_identifier(name)) << c.op;
        EXPECT_TRUE(seen.insert(name).second) << "duplicate test name for " << c.op;
    }
}

TEST(TestBaseName, ToleratesSpacesInTheSpelling) {
    EXPECT_EQ(test_base_name("operator =="), "OperatorEq");
    EXPECT_EQ(test_base_name("operator  [ ]"), "OperatorIndex");
    EXPECT_EQ(test_base_name("operator ( )"), "OperatorCall");
    EXPECT_EQ(test_base_name("operator new [ ]"), "OperatorNewArray");
}

TEST(TestBaseName, NewAndDelete) {
    EXPECT_EQ(test_base_name("operator new"), "OperatorNew");
    EXPECT_EQ(test_base_name("operator new[]"), "OperatorNewArray");
    EXPECT_EQ(test_base_name("operator delete"), "OperatorDelete");
    EXPECT_EQ(test_base_name("operator delete[]"), "OperatorDeleteArray");
}

TEST(TestBaseName, ConversionOperatorsNameTheirTarget) {
    EXPECT_EQ(test_base_name("operator bool"), "ConversionToBool");
    EXPECT_EQ(test_base_name("operator int"), "ConversionToInt");
    EXPECT_EQ(test_base_name("operator std::string"), "ConversionToStdString");
    EXPECT_EQ(test_base_name("operator const char*"), "ConversionToConstCharPtr");
    EXPECT_EQ(test_base_name("operator unsigned int"), "ConversionToUnsignedInt");
    EXPECT_EQ(test_base_name("operator std::vector<int>"), "ConversionToStdVectorInt");
    EXPECT_EQ(test_base_name("operator uint32_t"), "ConversionToUint32T");
    EXPECT_EQ(test_base_name("operator Foo&"), "ConversionToFooRef");
    EXPECT_EQ(test_base_name("operator newtype"), "ConversionToNewtype");
}

TEST(TestBaseName, UserDefinedLiteralsAndTheUnknown) {
    EXPECT_EQ(test_base_name("operator\"\"_km"), "OperatorLiteralKm");
    EXPECT_EQ(test_base_name("operator@"), "OperatorUnknown");
}

TEST(TypeSuffix, NamesTypesNotPassingConventions) {
    EXPECT_EQ(type_suffix({}), "");
    EXPECT_EQ(type_suffix({"int"}), "int");
    EXPECT_EQ(type_suffix({"const std::string&"}), "std_string");
    EXPECT_EQ(type_suffix({"std::string&&"}), "std_string");
    EXPECT_EQ(type_suffix({"std::vector<int>"}), "std_vector_int");
    EXPECT_EQ(type_suffix({"Foo*"}), "foo_ptr");
    EXPECT_EQ(type_suffix({"const char*"}), "char_ptr");
    EXPECT_EQ(type_suffix({"unsigned int"}), "unsigned_int");
    EXPECT_EQ(type_suffix({"MyType"}), "my_type");
    EXPECT_EQ(type_suffix({"int", "double"}), "int_double");
    EXPECT_EQ(type_suffix({"std::function<void(int)>"}), "std_function_void_int");
}

TEST(UniqueTestNames, UniqueBasesStayPlain) {
    const auto names = unique_test_names({{"size", {}}, {"empty", {}}, {"at", {"std::size_t"}}});
    EXPECT_EQ(names, (std::vector<std::string>{"size", "empty", "at"}));
}

TEST(UniqueTestNames, OverloadsAreToldApartByTheirTypes) {
    const auto names = unique_test_names({{"set", {"int"}}, {"set", {"const std::string&"}}, {"get", {}}});
    EXPECT_EQ(names, (std::vector<std::string>{"set_int", "set_std_string", "get"}));
}

TEST(UniqueTestNames, ASuffixLessOverloadKeepsThePlainName) {
    const auto names = unique_test_names({{"reset", {}}, {"reset", {"int"}}});
    EXPECT_EQ(names, (std::vector<std::string>{"reset", "reset_int"}));
}

TEST(UniqueTestNames, WhatStillCollidesGetsANumber) {
    // size() and size() const; set(int) and set(const int&).
    const auto names = unique_test_names({{"size", {}}, {"size", {}}, {"size", {}},
                                          {"set", {"int"}}, {"set", {"const int&"}}});
    EXPECT_EQ(names, (std::vector<std::string>{"size", "size_2", "size_3", "set_int", "set_int_2"}));
}

TEST(UniqueTestNames, ANumberNeverCollidesWithAnotherMethodsName) {
    const auto names = unique_test_names({{"size", {}}, {"size", {}}, {"size_2", {}}});
    EXPECT_EQ(names.size(), 3u);
    EXPECT_EQ(std::set<std::string>(names.begin(), names.end()).size(), 3u);
    EXPECT_EQ(names[0], "size");
    EXPECT_EQ(names[1], "size_2");
    EXPECT_EQ(names[2], "size_2_2");
}

TEST(UniqueTestNames, SuffixedNameAgainstAnExistingMethod) {
    const auto names = unique_test_names({{"set", {"int"}}, {"set", {"double"}}, {"set_int", {}}});
    EXPECT_EQ(std::set<std::string>(names.begin(), names.end()).size(), 3u);
    EXPECT_EQ(names[0], "set_int");
    EXPECT_EQ(names[2], "set_int_2");
}

TEST(UniqueTestNames, OperatorsGoThroughTheSamePath) {
    const auto names = unique_test_names({{test_base_name("operator-"), {}},
                                          {test_base_name("operator-"), {"const Foo&"}}});
    EXPECT_EQ(names, (std::vector<std::string>{"OperatorMinus", "OperatorMinus_foo"}));
}

TEST(UniqueTestNames, EverythingIsAValidIdentifierAndDeterministic) {
    const std::vector<TestNameInput> in = {
        {test_base_name("operator=="), {"const Foo&"}}, {test_base_name("operator bool"), {}},
        {"Constructs", {}}, {"Constructs", {"int"}}, {"Constructs", {"const char*"}},
    };
    const auto a = unique_test_names(in);
    const auto b = unique_test_names(in);
    EXPECT_EQ(a, b);
    for (const std::string& n : a) {
        EXPECT_TRUE(is_identifier(n)) << n;
    }
    EXPECT_EQ(a[3], "Constructs_int");
    EXPECT_EQ(a[4], "Constructs_char_ptr");
}

TEST(UniqueTestNames, Empty) {
    EXPECT_TRUE(unique_test_names({}).empty());
}
