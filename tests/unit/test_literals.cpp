// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

#include "mirrortest/frontend/literals.h"
#include "mirrortest/syntax/extract.h"

using namespace mt;
using core::CxxStd;
using frontend::Literal;
using Kind = frontend::Literal::Kind;

namespace {

std::vector<std::string> codes(const Literal& l) {
    std::vector<std::string> out;
    for (const auto& w : l.warnings) out.push_back(w.code);
    return out;
}

using Codes = std::vector<std::string>;
const Codes none = {};
const Codes range = {"mt-literal-range"};
const Codes literal = {"mt-literal"};
const Codes std_version = {"mt-std"};

struct Row {
    const char* text;
    const char* type;     // expected spelling, "" for none
    Codes       warnings;
};

void check(const Row& row, Literal (*classify)(std::string_view, CxxStd), CxxStd standard,
           Kind kind) {
    const Literal l = classify(row.text, standard);
    EXPECT_EQ(l.kind, kind) << row.text;
    EXPECT_EQ(l.type.spelling, row.type) << row.text;
    EXPECT_EQ(codes(l), row.warnings) << row.text;
    if (!l.type.empty()) {
        EXPECT_EQ(l.type.origin, ir::Origin::Inferred) << row.text;
        EXPECT_EQ(l.type.reason, std::string("literal ") + row.text) << row.text;
    }
}

void check_integers(const std::vector<Row>& rows, CxxStd standard = CxxStd::Cxx17) {
    for (const Row& row : rows) check(row, frontend::classify_number, standard, Kind::Integer);
}

}  // namespace

// ---- integers without a suffix ------------------------------------------------------------

TEST(LiteralsInteger, DecimalThatFitsAnInt) {
    check_integers({{"0", "int", none}, {"3", "int", none}, {"42", "int", none},
                    {"2147483647", "int", none}});
}

TEST(LiteralsInteger, DecimalThatDoesNotFitIsLongLongBecauseLongDependsOnTheTarget) {
    // long is 32 bits on ARM32 and 64 on LP64: never guessed from a magnitude.
    check_integers({{"2147483648", "long long", range},
                    {"4294967296", "long long", range},
                    {"9223372036854775807", "long long", range},
                    {"9223372036854775808", "unsigned long long", range}});
}

TEST(LiteralsInteger, HexadecimalOctalAndBinaryGoUnsignedBeforeGoingLong) {
    check_integers({{"0x7FFFFFFF", "int", none},
                    {"0x80000000", "unsigned int", none},     // the same in ILP32 and LP64
                    {"0xFFFFFFFF", "unsigned int", none},
                    {"0x100000000", "long long", range},
                    {"0x7FFFFFFFFFFFFFFF", "long long", range},
                    {"0x8000000000000000", "unsigned long long", range},
                    {"0xFFFFFFFFFFFFFFFF", "unsigned long long", range}});
}

TEST(LiteralsInteger, BasesAndTheirDigits) {
    const Literal oct = frontend::classify_number("017", CxxStd::Cxx17);
    EXPECT_EQ(oct.magnitude, 15u) << "017 is octal";
    EXPECT_EQ(oct.type.spelling, "int");
    EXPECT_EQ(frontend::classify_number("0377", CxxStd::Cxx17).magnitude, 255u);
    EXPECT_EQ(frontend::classify_number("0", CxxStd::Cxx17).magnitude, 0u);
    EXPECT_EQ(frontend::classify_number("0XFF", CxxStd::Cxx17).magnitude, 255u);
    EXPECT_EQ(frontend::classify_number("0xabCDef", CxxStd::Cxx17).magnitude, 0xABCDEFu);
    EXPECT_EQ(frontend::classify_number("0b101", CxxStd::Cxx17).magnitude, 5u);
    EXPECT_EQ(frontend::classify_number("0B11", CxxStd::Cxx17).magnitude, 3u);
}

TEST(LiteralsInteger, HexLiteralsContainingAnEAreStillIntegers) {
    // 0x1e5 is 485, not a floating-point number with an exponent.
    const Literal l = frontend::classify_number("0x1e5", CxxStd::Cxx17);
    EXPECT_EQ(l.kind, Kind::Integer);
    EXPECT_EQ(l.magnitude, 485u);
}

TEST(LiteralsInteger, ADigitThatIsNotOctalIsRejected) {
    const Literal l = frontend::classify_number("08", CxxStd::Cxx17);
    EXPECT_EQ(l.kind, Kind::Unsupported);
    EXPECT_EQ(codes(l), literal);
    EXPECT_TRUE(l.type.empty());
}

TEST(LiteralsInteger, TheSignIsPartOfTheTokenAndDoesNotChangeTheType) {
    const Literal minus = frontend::classify_number("-1", CxxStd::Cxx17);
    EXPECT_TRUE(minus.is_negative);
    EXPECT_EQ(minus.magnitude, 1u);
    EXPECT_EQ(minus.type.spelling, "int");
    EXPECT_EQ(minus.type.reason, "literal -1");

    EXPECT_FALSE(frontend::classify_number("+2", CxxStd::Cxx17).is_negative);
    EXPECT_EQ(frontend::classify_number("-1u", CxxStd::Cxx17).type.spelling, "unsigned int")
        << "-1u stays unsigned";
    EXPECT_EQ(frontend::classify_number("-2147483648", CxxStd::Cxx17).type.spelling, "long long")
        << "2147483648 is not an int, whatever sign is put in front of it";
}

// ---- integer suffixes --------------------------------------------------------------------------

TEST(LiteralsInteger, UnsignedSuffix) {
    check_integers({{"3u", "unsigned int", none}, {"3U", "unsigned int", none},
                    {"4294967295u", "unsigned int", none},
                    {"4294967296u", "unsigned long long", range}});
}

TEST(LiteralsInteger, LongSuffixIsTheOnlyWayToGetALong) {
    check_integers({{"3l", "long", none}, {"3L", "long", none}, {"2147483647l", "long", none},
                    // Fine on x86-64, but a 32-bit long on ARM32 cannot hold it.
                    {"5000000000l", "long", range}, {"2147483648l", "long", range},
                    {"3ul", "unsigned long", none}, {"3lu", "unsigned long", none},
                    {"3UL", "unsigned long", none}, {"4294967295ul", "unsigned long", none},
                    {"4294967296ul", "unsigned long", range}});
}

TEST(LiteralsInteger, LongLongSuffix) {
    check_integers({{"3ll", "long long", none}, {"3LL", "long long", none},
                    {"3ull", "unsigned long long", none}, {"3llu", "unsigned long long", none},
                    {"18446744073709551615ull", "unsigned long long", none},
                    {"0xFFu", "unsigned int", none}, {"0xFFul", "unsigned long", none},
                    {"0x7fffffffffffffffll", "long long", none}});
}

TEST(LiteralsInteger, SizeTSuffixIsC23) {
    check_integers({{"3uz", "std::size_t", none}, {"3zu", "std::size_t", none},
                    {"3z", "std::ptrdiff_t", none}},
                   CxxStd::Cxx23);
    check_integers({{"3uz", "std::size_t", std_version}, {"3z", "std::ptrdiff_t", std_version}},
                   CxxStd::Cxx17);
}

TEST(LiteralsInteger, TooLargeForAnyType) {
    const Literal l = frontend::classify_number("99999999999999999999999", CxxStd::Cxx17);
    EXPECT_EQ(l.kind, Kind::Integer);
    EXPECT_EQ(l.type.spelling, "unsigned long long");
    EXPECT_EQ(codes(l), literal);
    EXPECT_EQ(l.magnitude, std::numeric_limits<unsigned long long>::max());
}

TEST(LiteralsInteger, InvalidAndUserDefinedSuffixes) {
    for (const char* text : {"3f", "3uu", "3lul", "3lll", "3xyz"}) {
        const Literal l = frontend::classify_number(text, CxxStd::Cxx17);
        EXPECT_EQ(l.kind, Kind::Unsupported) << text;
        EXPECT_EQ(codes(l), literal) << text;
    }
    const Literal user = frontend::classify_number("3_km", CxxStd::Cxx17);
    EXPECT_EQ(user.kind, Kind::Unsupported);
    EXPECT_NE(user.warnings[0].message.find("user-defined literal suffix '_km'"), std::string::npos);
}

TEST(LiteralsInteger, DigitSeparatorsAndBinaryAreC14) {
    check_integers({{"1'000'000", "int", none}, {"0x7F'FF", "int", none}, {"0b1010", "int", none}});
    check_integers({{"1'000", "int", std_version}, {"0b1", "int", std_version}}, CxxStd::Cxx11);
    EXPECT_EQ(frontend::classify_number("1'000'000", CxxStd::Cxx17).magnitude, 1000000u);
    EXPECT_EQ(frontend::classify_number("0x7F'FF", CxxStd::Cxx17).magnitude, 0x7FFFu);
}

TEST(LiteralsInteger, ChronoLiterals) {
    check_integers({{"1h", "std::chrono::hours", none}, {"2min", "std::chrono::minutes", none},
                    {"3s", "std::chrono::seconds", none}, {"10ms", "std::chrono::milliseconds", none},
                    {"4us", "std::chrono::microseconds", none}, {"5ns", "std::chrono::nanoseconds", none}});
    check_integers({{"10ms", "std::chrono::milliseconds", std_version}}, CxxStd::Cxx11);
}

// ---- floating point ------------------------------------------------------------------------------

TEST(LiteralsFloating, TypesBySuffix) {
    for (const Row& row : std::vector<Row>{
             {"1.5", "double", none}, {".5", "double", none}, {"2.", "double", none},
             {"1e5", "double", none}, {"1E5", "double", none}, {"1e-9", "double", none},
             {"1.5e+3", "double", none}, {"1.5f", "float", none}, {"1.5F", "float", none},
             {".5f", "float", none}, {"2.f", "float", none}, {"1e5f", "float", none},
             {"1.5l", "long double", none}, {"1.0L", "long double", none},
             {"1'000.5", "double", none}}) {
        check(row, frontend::classify_number, CxxStd::Cxx17, Kind::Floating);
    }
}

TEST(LiteralsFloating, HexadecimalFloats) {
    for (const Row& row : std::vector<Row>{{"0x1p3", "double", none}, {"0x1.8p1", "double", none},
                                           {"0x1p-2", "double", none}, {"0x1p3f", "float", none},
                                           {"0x.8p1L", "long double", none}}) {
        check(row, frontend::classify_number, CxxStd::Cxx17, Kind::Floating);
    }
}

TEST(LiteralsFloating, TheSignIsRecorded) {
    const Literal l = frontend::classify_number("-1.5", CxxStd::Cxx17);
    EXPECT_EQ(l.kind, Kind::Floating);
    EXPECT_TRUE(l.is_negative);
    EXPECT_EQ(l.type.spelling, "double");
}

TEST(LiteralsFloating, UnsupportedSuffixes) {
    for (const char* text : {"1.5_km", "1.5f16", "1.5f128", "1.5bf16", "1.5s", "1.5ms", "1.5x"}) {
        const Literal l = frontend::classify_number(text, CxxStd::Cxx23);
        EXPECT_EQ(l.kind, Kind::Unsupported) << text;
        EXPECT_TRUE(l.type.empty()) << text;
        EXPECT_EQ(codes(l), literal) << text;
    }
}

// ---- characters -------------------------------------------------------------------------------------

TEST(LiteralsCharacter, TypesByPrefix) {
    for (const Row& row : std::vector<Row>{
             {"'a'", "char", none}, {"'\\n'", "char", none}, {"'\\''", "char", none},
             {"'\\\\'", "char", none}, {"'\\x41'", "char", none}, {"'\\101'", "char", none},
             {"'\\u00e9'", "char", none}, {"u8'a'", "char", none}, {"u'a'", "char16_t", none},
             {"U'a'", "char32_t", none}, {"L'a'", "wchar_t", none}}) {
        check(row, frontend::classify_character, CxxStd::Cxx17, Kind::Character);
    }
}

TEST(LiteralsCharacter, U8CharactersAreChar8TInC20AndNeedC17) {
    EXPECT_EQ(frontend::classify_character("u8'a'", CxxStd::Cxx20).type.spelling, "char8_t");
    EXPECT_EQ(frontend::classify_character("u8'a'", CxxStd::Cxx17).type.spelling, "char");
    EXPECT_EQ(codes(frontend::classify_character("u8'a'", CxxStd::Cxx14)), std_version);
}

TEST(LiteralsCharacter, MultiCharacterLiteralsAreIntWithAWarning) {
    for (const char* text : {"'ab'", "'\\n\\n'", "'abcd'", "'a\\n'"}) {
        const Literal l = frontend::classify_character(text, CxxStd::Cxx17);
        EXPECT_EQ(l.type.spelling, "int") << text;
        EXPECT_EQ(codes(l), literal) << text;
    }
    // The escape is one character, not several.
    EXPECT_EQ(frontend::classify_character("'\\x41'", CxxStd::Cxx17).type.spelling, "char");
    EXPECT_EQ(frontend::classify_character("'\\x41\\x42'", CxxStd::Cxx17).type.spelling, "int");
}

TEST(LiteralsCharacter, MalformedIsUnsupported) {
    EXPECT_EQ(frontend::classify_character("'", CxxStd::Cxx17).kind, Kind::Unsupported);
    EXPECT_EQ(frontend::classify_character("x'a'", CxxStd::Cxx17).kind, Kind::Unsupported);
}

// ---- strings -----------------------------------------------------------------------------------------

TEST(LiteralsString, AnOrdinaryStringHasNoTypeOfItsOwn) {
    // "x" is const char*, std::string or QString depending on the assertion.
    for (const char* text : {"\"x\"", "\"\"", "R\"(x)\"", "R\"d(a\"b)d\"", "\"a\" \"b\"",
                             "\"say \\\"hi\\\"\""}) {
        const Literal l = frontend::classify_string(text, CxxStd::Cxx17);
        EXPECT_EQ(l.kind, Kind::String) << text;
        EXPECT_TRUE(l.type.empty()) << text;
        EXPECT_TRUE(l.warnings.empty()) << text;
    }
}

TEST(LiteralsString, PrefixedStringsAreTypedPointers) {
    for (const Row& row : std::vector<Row>{{"L\"x\"", "const wchar_t*", none},
                                           {"u\"x\"", "const char16_t*", none},
                                           {"U\"x\"", "const char32_t*", none},
                                           {"u8\"x\"", "const char*", none},
                                           {"u8R\"(x)\"", "const char*", none},
                                           {"LR\"(x)\"", "const wchar_t*", none}}) {
        check(row, frontend::classify_string, CxxStd::Cxx17, Kind::String);
    }
    EXPECT_EQ(frontend::classify_string("u8\"x\"", CxxStd::Cxx20).type.spelling, "const char8_t*");

    const Literal wide = frontend::classify_string("u\"x\"", CxxStd::Cxx17);
    EXPECT_EQ(wide.type.base, "char16_t");
    EXPECT_TRUE(wide.type.is_const);
    EXPECT_EQ(wide.type.pointer_depth, 1u);
}

TEST(LiteralsString, StandardSuffixes) {
    check({"\"x\"s", "std::string", none}, frontend::classify_string, CxxStd::Cxx17, Kind::String);
    check({"\"x\"sv", "std::string_view", none}, frontend::classify_string, CxxStd::Cxx17, Kind::String);
    check({"L\"x\"s", "std::wstring", none}, frontend::classify_string, CxxStd::Cxx17, Kind::String);
    check({"u\"x\"sv", "std::u16string_view", none}, frontend::classify_string, CxxStd::Cxx17, Kind::String);
    check({"U\"x\"s", "std::u32string", none}, frontend::classify_string, CxxStd::Cxx17, Kind::String);
    check({"u8\"x\"s", "std::u8string", none}, frontend::classify_string, CxxStd::Cxx20, Kind::String);
    check({"u8\"x\"s", "std::string", none}, frontend::classify_string, CxxStd::Cxx17, Kind::String);
    check({"R\"(x)\"s", "std::string", none}, frontend::classify_string, CxxStd::Cxx17, Kind::String);
}

TEST(LiteralsString, SuffixesNeedTheirStandard) {
    EXPECT_EQ(codes(frontend::classify_string("\"x\"s", CxxStd::Cxx11)), std_version);
    EXPECT_TRUE(frontend::classify_string("\"x\"s", CxxStd::Cxx14).warnings.empty());
    EXPECT_EQ(codes(frontend::classify_string("\"x\"sv", CxxStd::Cxx14)), std_version);
    EXPECT_TRUE(frontend::classify_string("\"x\"sv", CxxStd::Cxx17).warnings.empty());
}

TEST(LiteralsString, UserDefinedSuffixesAreUnsupported) {
    const Literal l = frontend::classify_string("\"x\"_name", CxxStd::Cxx17);
    EXPECT_EQ(l.kind, Kind::Unsupported);
    EXPECT_EQ(codes(l), literal);
}

TEST(LiteralsString, Malformed) {
    EXPECT_EQ(frontend::classify_string("x", CxxStd::Cxx17).kind, Kind::Unsupported);
    EXPECT_EQ(frontend::classify_string("\"", CxxStd::Cxx17).kind, Kind::Unsupported);
    EXPECT_EQ(frontend::classify_string("q\"x\"", CxxStd::Cxx17).kind, Kind::Unsupported);
}

// ---- on real nodes ------------------------------------------------------------------------------------------

namespace {

// The arguments of the single call in `void f() { g(<args>); }`.
struct Args {
    explicit Args(const std::string& args) : tree(syntax::parse_cpp("void f() {\n  g(" + args + ");\n}\n")) {
        const syntax::Node call = syntax::find_first(tree.root(), "call_expression");
        nodes = call.child_by_field("arguments").named_children();
    }
    syntax::Tree tree;
    std::vector<syntax::Node> nodes;
};

}  // namespace

TEST(LiteralsNodes, EveryLiteralKindIsRecognised) {
    const Args a("3u, 'a', \"x\", true, false, nullptr, 10ms, \"x\"s, -1, R\"(x)\", 1.5f");
    ASSERT_EQ(a.nodes.size(), 11u);
    const auto classify = [&](std::size_t i) {
        return frontend::classify_literal(a.nodes[i], CxxStd::Cxx17, "tests/foo_test.cpp");
    };

    EXPECT_EQ(classify(0)->type.spelling, "unsigned int");
    EXPECT_EQ(classify(1)->type.spelling, "char");
    EXPECT_EQ(classify(2)->kind, Kind::String);
    EXPECT_EQ(classify(3)->kind, Kind::Boolean);
    EXPECT_EQ(classify(3)->type.spelling, "bool");
    EXPECT_EQ(classify(4)->type.reason, "literal false");
    EXPECT_EQ(classify(5)->kind, Kind::NullPointer);
    EXPECT_TRUE(classify(5)->type.empty());
    EXPECT_EQ(classify(6)->type.spelling, "std::chrono::milliseconds");
    EXPECT_EQ(classify(7)->type.spelling, "std::string");
    EXPECT_EQ(classify(8)->type.spelling, "int");
    EXPECT_TRUE(classify(8)->is_negative);
    EXPECT_EQ(classify(9)->kind, Kind::String);
    EXPECT_EQ(classify(10)->type.spelling, "float");
}

TEST(LiteralsNodes, TheTypeCarriesThePositionOfTheLiteral) {
    const Args a("1, 3u");
    const auto l = frontend::classify_literal(a.nodes[1], CxxStd::Cxx17, "tests/foo_test.cpp");
    ASSERT_TRUE(l.has_value());
    EXPECT_EQ(l->type.where.file, "tests/foo_test.cpp");
    EXPECT_EQ(l->type.where.line, 2u);
    EXPECT_EQ(l->type.where.column, 8u);   // "  g(1, 3u);"
}

TEST(LiteralsNodes, UserDefinedLiteralsGoThroughTheirOwnKind) {
    const Args a("1.5_km, \"x\"_s, 'a'_c, 7_ms");
    ASSERT_EQ(a.nodes.size(), 4u);
    for (std::size_t i = 0; i < a.nodes.size(); ++i) {
        const auto l = frontend::classify_literal(a.nodes[i], CxxStd::Cxx17, "f.cpp");
        ASSERT_TRUE(l.has_value()) << i;
        EXPECT_EQ(l->kind, Kind::Unsupported) << a.nodes[i].text();
        EXPECT_FALSE(l->warnings.empty()) << a.nodes[i].text();
    }
}

TEST(LiteralsNodes, ExpressionsAreNotLiterals) {
    // Typing these needs more than the text: that is the test frontend's job.
    const Args a("x, - 3, 3 + 4, sizeof(int), (unsigned)3, static_cast<int>(x), std::size_t{3}, Foo::X, int(3.5), f()");
    ASSERT_EQ(a.nodes.size(), 10u);
    for (const syntax::Node& n : a.nodes) {
        EXPECT_FALSE(frontend::classify_literal(n, CxxStd::Cxx17, "f.cpp").has_value()) << n.text();
    }
}
