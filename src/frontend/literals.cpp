// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/frontend/literals.h"

#include <algorithm>
#include <limits>

#include "mirrortest/core/diagnostics.h"
#include "mirrortest/ir/type.h"
#include "mirrortest/syntax/extract.h"

namespace mt::frontend {

namespace {

using core::CxxStd;
using ull = unsigned long long;

constexpr ull kIntMax = 2147483647ull;
constexpr ull kUintMax = 4294967295ull;
constexpr ull kLlongMax = 9223372036854775807ull;
constexpr ull kUllongMax = std::numeric_limits<ull>::max();

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

int digit_value(char c, int base) {
    int v = -1;
    if (is_digit(c)) v = c - '0';
    else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
    return v >= 0 && v < base ? v : -1;
}

ir::Type inferred(std::string_view base, bool is_const, unsigned pointers, const std::string& text) {
    ir::Type t = ir::make_type(base, is_const, ir::RefKind::None, pointers);
    t.origin = ir::Origin::Inferred;
    t.reason = "literal " + text;
    return t;
}

void warn(Literal& lit, const char* code, std::string message) {
    lit.warnings.push_back({code, std::move(message)});
}

void unsupported(Literal& lit, std::string message) {
    lit.kind = Literal::Kind::Unsupported;
    lit.type = ir::Type();
    warn(lit, core::code::literal, std::move(message));
}

void need_std(Literal& lit, CxxStd actual, CxxStd required, const std::string& feature) {
    if (core::to_number(actual) < core::to_number(required)) {
        warn(lit, core::code::std_version,
             feature + " needs C++" + std::to_string(core::to_number(required)) + ", but --std is " +
                 core::to_string(actual));
    }
}

// std::chrono duration literals: `10ms`. Only integers; see classify_number.
const char* chrono_type(std::string_view suffix) {
    if (suffix == "h") return "std::chrono::hours";
    if (suffix == "min") return "std::chrono::minutes";
    if (suffix == "s") return "std::chrono::seconds";
    if (suffix == "ms") return "std::chrono::milliseconds";
    if (suffix == "us") return "std::chrono::microseconds";
    if (suffix == "ns") return "std::chrono::nanoseconds";
    return nullptr;
}

struct IntSuffix {
    bool ok = false;
    bool is_unsigned = false;
    int  longs = 0;
    bool size_t_suffix = false;   // C++23 z
};

IntSuffix parse_int_suffix(const std::string& s) {
    IntSuffix r;
    r.ok = true;
    if (s.empty()) return r;
    if (s == "u") { r.is_unsigned = true; return r; }
    if (s == "l") { r.longs = 1; return r; }
    if (s == "ll") { r.longs = 2; return r; }
    if (s == "ul" || s == "lu") { r.is_unsigned = true; r.longs = 1; return r; }
    if (s == "ull" || s == "llu") { r.is_unsigned = true; r.longs = 2; return r; }
    if (s == "z") { r.size_t_suffix = true; return r; }
    if (s == "uz" || s == "zu") { r.is_unsigned = true; r.size_t_suffix = true; return r; }
    r.ok = false;
    return r;
}

// An unrecognised suffix: user-defined, or just wrong.
void bad_suffix(Literal& lit, const std::string& suffix) {
    if (!suffix.empty() && suffix[0] == '_') {
        unsupported(lit, "user-defined literal suffix '" + suffix + "': its type is unknown");
    } else {
        unsupported(lit, "'" + suffix + "' is not a valid literal suffix here");
    }
}

void classify_integer(Literal& lit, const std::string& low, bool hex, bool bin, CxxStd standard) {
    int base = hex ? 16 : bin ? 2 : (low.size() > 1 && low[0] == '0') ? 8 : 10;
    std::size_t pos = (hex || bin) ? 2 : 0;
    const std::size_t first_digit = pos;

    ull value = 0;
    bool overflow = false;
    for (; pos < low.size(); ++pos) {
        const int d = digit_value(low[pos], base);
        if (d < 0) break;
        if (value > (kUllongMax - static_cast<ull>(d)) / static_cast<ull>(base)) {
            overflow = true;
        } else {
            value = value * static_cast<ull>(base) + static_cast<ull>(d);
        }
    }
    if (pos == first_digit) {
        unsupported(lit, "malformed integer literal");
        return;
    }
    if (base == 8 && pos < low.size() && (low[pos] == '8' || low[pos] == '9')) {
        unsupported(lit, "digit '" + std::string(1, low[pos]) + "' in an octal literal");
        return;
    }
    if (bin) {
        need_std(lit, standard, CxxStd::Cxx14, "a binary literal");
    }

    lit.magnitude = overflow ? kUllongMax : value;
    const std::string suffix = low.substr(pos);
    const bool decimal = base == 10;

    const IntSuffix s = parse_int_suffix(suffix);
    if (!s.ok) {
        if (const char* chrono = chrono_type(suffix)) {
            lit.kind = Literal::Kind::Integer;
            lit.type = inferred(chrono, false, 0, lit.text);
            need_std(lit, standard, CxxStd::Cxx14, "a std::chrono literal");
            return;
        }
        bad_suffix(lit, suffix);
        return;
    }

    lit.kind = Literal::Kind::Integer;
    const std::string& text = lit.text;
    const auto range_note = [&](const char* why) {
        warn(lit, core::code::literal_range, std::string(why));
    };

    if (overflow) {
        lit.type = inferred("unsigned long long", false, 0, text);
        warn(lit, core::code::literal, "integer literal is too large for any integer type");
        return;
    }

    if (s.size_t_suffix) {
        need_std(lit, standard, CxxStd::Cxx23, "the 'z' literal suffix");
        lit.type = inferred(s.is_unsigned ? "std::size_t" : "std::ptrdiff_t", false, 0, text);
        return;
    }

    if (s.longs == 2) {
        lit.type = inferred(s.is_unsigned ? "unsigned long long" : "long long", false, 0, text);
        if (!s.is_unsigned && value > kLlongMax) {
            range_note("does not fit a long long");
        }
        return;
    }

    if (s.longs == 1) {
        lit.type = inferred(s.is_unsigned ? "unsigned long" : "long", false, 0, text);
        if (value > (s.is_unsigned ? kUintMax : kIntMax)) {
            range_note("does not fit a 32-bit long, which is what it is on 32-bit targets and on 64-bit Windows");
        }
        return;
    }

    if (s.is_unsigned) {
        if (value <= kUintMax) {
            lit.type = inferred("unsigned int", false, 0, text);
        } else {
            lit.type = inferred("unsigned long long", false, 0, text);
            range_note("does not fit an unsigned int; its type depends on the data model");
        }
        return;
    }

    // No suffix.
    if (value <= kIntMax) {
        lit.type = inferred("int", false, 0, text);
    } else if (!decimal && value <= kUintMax) {
        lit.type = inferred("unsigned int", false, 0, text);
    } else if (value <= kLlongMax) {
        lit.type = inferred("long long", false, 0, text);
        range_note("does not fit an int; the standard types it as long or long long depending on the data model");
    } else {
        lit.type = inferred("unsigned long long", false, 0, text);
        range_note("does not fit a long long; the standard types it differently on 32- and 64-bit targets");
    }
}

// Where the numeric part of a floating literal ends.
std::size_t float_body_end(const std::string& low, bool hex) {
    std::size_t pos = hex ? 2 : 0;
    const int base = hex ? 16 : 10;
    while (pos < low.size() && digit_value(low[pos], base) >= 0) ++pos;
    if (pos < low.size() && low[pos] == '.') {
        ++pos;
        while (pos < low.size() && digit_value(low[pos], base) >= 0) ++pos;
    }
    const char exponent = hex ? 'p' : 'e';
    if (pos < low.size() && low[pos] == exponent) {
        std::size_t p = pos + 1;
        if (p < low.size() && (low[p] == '+' || low[p] == '-')) ++p;
        if (p < low.size() && is_digit(low[p])) {
            while (p < low.size() && is_digit(low[p])) ++p;
            pos = p;
        }
    }
    return pos;
}

void classify_floating(Literal& lit, const std::string& low, bool hex) {
    const std::string suffix = low.substr(float_body_end(low, hex));
    lit.kind = Literal::Kind::Floating;
    if (suffix.empty()) {
        lit.type = inferred("double", false, 0, lit.text);
    } else if (suffix == "f") {
        lit.type = inferred("float", false, 0, lit.text);
    } else if (suffix == "l") {
        lit.type = inferred("long double", false, 0, lit.text);
    } else if (suffix == "f16" || suffix == "f32" || suffix == "f64" || suffix == "f128" || suffix == "bf16") {
        unsupported(lit, "the C++23 floating-point literal suffix '" + suffix + "' is not supported");
    } else if (chrono_type(suffix) != nullptr) {
        unsupported(lit, "floating-point std::chrono literals are not supported");
    } else {
        bad_suffix(lit, suffix);
    }
}

bool has_decimal_exponent(const std::string& low) {
    for (std::size_t i = 1; i < low.size(); ++i) {
        if (low[i] != 'e' || !(is_digit(low[i - 1]) || low[i - 1] == '.')) continue;
        std::size_t p = i + 1;
        if (p < low.size() && (low[p] == '+' || low[p] == '-')) ++p;
        if (p < low.size() && is_digit(low[p])) return true;
    }
    return false;
}

// The number of characters a character literal's body denotes, escapes counted
// as one: 'a' is 1, '\n' is 1, '\x41' is 1, 'ab' is 2.
unsigned character_count(std::string_view body) {
    unsigned count = 0;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(body[i]);
        if (c == '\\' && i + 1 < body.size()) {
            const char e = body[++i];
            if (e == 'x') {
                while (i + 1 < body.size() && digit_value(body[i + 1], 16) >= 0) ++i;
            } else if (e >= '0' && e <= '7') {
                for (int k = 0; k < 2 && i + 1 < body.size() && body[i + 1] >= '0' && body[i + 1] <= '7'; ++k) ++i;
            } else if (e == 'u' || e == 'U') {
                for (int k = 0; k < (e == 'u' ? 4 : 8) && i + 1 < body.size() && digit_value(body[i + 1], 16) >= 0; ++k) ++i;
            }
            ++count;
        } else if ((c & 0xC0) != 0x80) {
            ++count;   // a UTF-8 continuation byte is not another character
        }
    }
    return count;
}

const char* string_type(const std::string& char_type, bool view) {
    static const struct { const char* ch; const char* str; const char* sv; } table[] = {
        {"char", "std::string", "std::string_view"},
        {"char8_t", "std::u8string", "std::u8string_view"},
        {"char16_t", "std::u16string", "std::u16string_view"},
        {"char32_t", "std::u32string", "std::u32string_view"},
        {"wchar_t", "std::wstring", "std::wstring_view"},
    };
    for (const auto& row : table) {
        if (char_type == row.ch) return view ? row.sv : row.str;
    }
    return "std::string";
}

}  // namespace

Literal classify_number(std::string_view raw, CxxStd standard) {
    Literal lit;
    lit.text = std::string(raw);

    std::string body(raw);
    if (!body.empty() && (body[0] == '-' || body[0] == '+')) {
        lit.is_negative = body[0] == '-';
        body.erase(0, 1);
    }
    if (body.find('\'') != std::string::npos) {
        need_std(lit, standard, CxxStd::Cxx14, "digit separators");
        body.erase(std::remove(body.begin(), body.end(), '\''), body.end());
    }
    const std::string low = lower(body);
    if (low.empty() || !(is_digit(low[0]) || (low[0] == '.' && low.size() > 1 && is_digit(low[1])))) {
        unsupported(lit, "malformed numeric literal");
        return lit;
    }

    const bool hex = starts_with(low, "0x");
    const bool bin = starts_with(low, "0b");
    const bool floating = hex ? low.find('p') != std::string::npos
                              : bin ? false
                                    : low.find('.') != std::string::npos || has_decimal_exponent(low);
    if (floating) {
        classify_floating(lit, low, hex);
    } else {
        classify_integer(lit, low, hex, bin, standard);
    }
    return lit;
}

Literal classify_character(std::string_view raw, CxxStd standard) {
    Literal lit;
    lit.text = std::string(raw);
    lit.kind = Literal::Kind::Character;

    const std::size_t open = raw.find('\'');
    const std::size_t close = raw.rfind('\'');
    if (open == std::string_view::npos || close == open) {
        unsupported(lit, "malformed character literal");
        return lit;
    }
    const std::string prefix(raw.substr(0, open));
    const std::string_view body = raw.substr(open + 1, close - open - 1);

    std::string type;
    if (prefix.empty()) {
        type = "char";
    } else if (prefix == "u8") {
        need_std(lit, standard, CxxStd::Cxx17, "a u8 character literal");
        type = core::to_number(standard) >= 20 ? "char8_t" : "char";
    } else if (prefix == "u") {
        type = "char16_t";
    } else if (prefix == "U") {
        type = "char32_t";
    } else if (prefix == "L") {
        type = "wchar_t";
    } else {
        unsupported(lit, "malformed character literal");
        return lit;
    }

    if (prefix.empty() && character_count(body) > 1) {
        type = "int";
        warn(lit, core::code::literal, "multi-character literal: its value is implementation-defined");
    }
    lit.type = inferred(type, false, 0, lit.text);
    return lit;
}

Literal classify_string(std::string_view raw, CxxStd standard) {
    Literal lit;
    lit.text = std::string(raw);
    lit.kind = Literal::Kind::String;

    const std::size_t open = raw.find('"');
    const std::size_t close = raw.rfind('"');
    if (open == std::string_view::npos || close == open) {
        unsupported(lit, "malformed string literal");
        return lit;
    }
    std::string prefix(raw.substr(0, open));
    if (!prefix.empty() && prefix.back() == 'R') prefix.pop_back();   // raw string
    const std::string suffix(raw.substr(close + 1));

    std::string char_type;
    if (prefix.empty()) char_type = "char";
    else if (prefix == "u8") char_type = core::to_number(standard) >= 20 ? "char8_t" : "char";
    else if (prefix == "u") char_type = "char16_t";
    else if (prefix == "U") char_type = "char32_t";
    else if (prefix == "L") char_type = "wchar_t";
    else {
        unsupported(lit, "malformed string literal");
        return lit;
    }

    if (suffix.empty()) {
        // An ordinary narrow string is "a string": which type depends on the
        // assertion it is compared in. Anything with a prefix has one.
        if (!prefix.empty()) {
            lit.type = inferred(char_type, true, 1, lit.text);
        }
    } else if (suffix == "s") {
        need_std(lit, standard, CxxStd::Cxx14, "a string literal with the 's' suffix");
        lit.type = inferred(string_type(char_type, false), false, 0, lit.text);
    } else if (suffix == "sv") {
        need_std(lit, standard, CxxStd::Cxx17, "a string literal with the 'sv' suffix");
        lit.type = inferred(string_type(char_type, true), false, 0, lit.text);
    } else {
        bad_suffix(lit, suffix);
    }
    return lit;
}

std::optional<Literal> classify_literal(const syntax::Node& node, CxxStd standard, std::string_view file) {
    const std::string_view type = node.type();
    const std::string text(node.text());
    std::optional<Literal> result;

    if (type == "number_literal") {
        result = classify_number(text, standard);
    } else if (type == "char_literal") {
        result = classify_character(text, standard);
    } else if (type == "string_literal" || type == "raw_string_literal" || type == "concatenated_string") {
        result = classify_string(text, standard);
    } else if (type == "user_defined_literal") {
        if (text.find('"') != std::string::npos) {
            result = classify_string(text, standard);
        } else {
            std::size_t i = (!text.empty() && (text[0] == '-' || text[0] == '+')) ? 1 : 0;
            if (i < text.size() && (is_digit(text[i]) || text[i] == '.')) {
                result = classify_number(text, standard);
            } else {
                Literal lit;
                lit.text = text;
                unsupported(lit, "user-defined character literal: its type is unknown");
                result = lit;
            }
        }
    } else if (type == "true" || type == "false") {
        Literal lit;
        lit.text = text;
        lit.kind = Literal::Kind::Boolean;
        lit.type = inferred("bool", false, 0, text);
        result = lit;
    } else if (type == "null") {
        Literal lit;
        lit.text = text;
        lit.kind = Literal::Kind::NullPointer;
        result = lit;
    }

    if (result && !result->type.empty()) {
        result->type.where = syntax::location(node, file);
    }
    return result;
}

}  // namespace mt::frontend
