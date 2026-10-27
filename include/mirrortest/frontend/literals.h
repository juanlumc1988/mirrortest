// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// What type is a literal? (docs/design.md, section 7.5.)
//
// gen-impl has no compiler to ask, so `foo.size()` compared against `3u` gives
// the return type `unsigned int` only because the literal says so. This is
// that table, and it is not the language's: it is the language's made safe for
// a tool whose output is built for a different data model than the one it runs
// on.
//
// The rule that shapes it (design R10): `long` is 32 bits on 32-bit targets and
// on 64-bit Windows and 64 on 64-bit Linux, and the generator runs on one data
// model while the code may be built for the other. So a `long` is only ever inferred from an explicit `l` suffix, never
// from the size of the value. A decimal literal that does not fit an `int`
// becomes `long long` and a hexadecimal one that does not fit an `unsigned
// int` also does, each with a warning, because the standard would give `long`
// on one target and `long long` on the other.
//
// Every inferred type carries Origin::Inferred and the reason
// "literal <text>", which is what ends up in the generated
// "TODO(mt): ... inferred from literal 3u" comment.
//
// Not here: casts, `sizeof`, `T{x}` and enumerators. Those are expressions with
// a type of their own, not literals; the frontend that reads tests types them.

#ifndef MT_FRONTEND_LITERALS_H
#define MT_FRONTEND_LITERALS_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mirrortest/core/config.h"
#include "mirrortest/ir/model.h"
#include "mirrortest/syntax/tree.h"

namespace mt::frontend {

struct LiteralWarning {
    std::string code;      // core::code::literal, literal_range or std_version
    std::string message;
};

struct Literal {
    enum class Kind {
        Integer,
        Floating,
        Character,
        String,        // see `type`: empty for an ordinary narrow string
        Boolean,
        NullPointer,   // no type: the pointee is unknown
        Unsupported,   // user-defined suffix, C++23 float types, malformed: see warnings
    };

    Kind kind = Kind::Unsupported;

    // The inferred type, Origin::Inferred. Empty for:
    //   * an ordinary narrow string ("x", R"(x)", "a" "b"): `const char*`,
    //     `std::string` or `QString` depending on the assertion it appears
    //     in (design 7.6), which only the caller knows;
    //   * nullptr, and anything Unsupported.
    ir::Type type;

    std::string text;                      // as written, sign included
    std::vector<LiteralWarning> warnings;  // for the caller to locate and report

    // Integer literals only.
    bool               is_negative = false;
    unsigned long long magnitude = 0;      // saturates at the maximum if it overflowed
};

// `text` is the whole literal token: "3u", "-1", "0xFFul", "1e-9", "10ms",
// "1.5_km". A leading '+' or '-' is part of the token (tree-sitter lexes `-1`
// as one literal). `standard` only decides which warnings apply: a C++14 feature
// under --std c++11 is an mt-std warning, not a different type.
Literal classify_number(std::string_view text, core::CxxStd standard);

// "'a'", "u8'a'", "L'a'", "'ab'" (multi-character: int, with a warning).
Literal classify_character(std::string_view text, core::CxxStd standard);

// A string token as tree-sitter gives it: prefixes (u8 u U L), raw strings,
// adjacent literals ("a" "b") and the standard suffixes ("x"s, "x"sv).
Literal classify_string(std::string_view text, core::CxxStd standard);

// Dispatches on the node type (number_literal, user_defined_literal,
// char_literal, string_literal, raw_string_literal, concatenated_string, true,
// false, null) and sets the type's position. Returns nullopt for any other
// node: it is not a literal.
std::optional<Literal> classify_literal(const syntax::Node& node, core::CxxStd standard,
                                        std::string_view file);

}  // namespace mt::frontend

#endif  // MT_FRONTEND_LITERALS_H
