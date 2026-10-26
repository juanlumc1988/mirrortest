// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/core/naming.h"

#include <algorithm>
#include <map>
#include <set>

namespace mt::core {

namespace {

bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_high(char c) { return static_cast<unsigned char>(c) >= 0x80; }
bool is_ident_start(char c) { return is_upper(c) || is_lower(c) || c == '_'; }
bool is_ident_char(char c) { return is_ident_start(c) || is_digit(c); }
bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
char to_lower(char c) { return is_upper(c) ? static_cast<char>(c - 'A' + 'a') : c; }
char to_upper(char c) { return is_lower(c) ? static_cast<char>(c - 'a' + 'A') : c; }

enum class Cls { Lower, Upper, Digit, Sep };

Cls classify(char c) {
    if (is_upper(c)) return Cls::Upper;
    if (is_lower(c) || is_high(c)) return Cls::Lower;
    if (is_digit(c)) return Cls::Digit;
    return Cls::Sep;
}

std::string trim(std::string_view s) {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return std::string(s);
}

std::string without_spaces(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (!is_space(c)) out += c;
    }
    return out;
}

// "uint32_t" -> "Uint32T", "string" -> "String", "MyType" -> "MyType".
std::string capitalise_words(std::string_view identifier) {
    std::string out;
    bool start = true;
    for (char c : identifier) {
        if (c == '_') {
            start = true;
            continue;
        }
        out += start ? to_upper(c) : c;
        start = false;
    }
    return out;
}

// "const char*" -> "ConstCharPtr", "std::vector<int>" -> "StdVectorInt".
std::string camel_type(std::string_view type) {
    std::string out;
    for (std::size_t i = 0; i < type.size();) {
        if (is_ident_char(type[i])) {
            std::size_t j = i;
            while (j < type.size() && is_ident_char(type[j])) ++j;
            out += capitalise_words(type.substr(i, j - i));
            i = j;
        } else if (type[i] == '*') {
            out += "Ptr";
            ++i;
        } else if (type[i] == '&') {
            if (i + 1 < type.size() && type[i + 1] == '&') {
                out += "RRef";
                i += 2;
            } else {
                out += "Ref";
                ++i;
            }
        } else {
            ++i;   // ':', '<', '>', ',' and spaces only separate words
        }
    }
    return out;
}

struct OperatorName {
    const char* symbol;
    const char* name;
};

const OperatorName kOperators[] = {
    {"==", "Eq"},       {"!=", "Ne"},        {"<=>", "Spaceship"}, {"<=", "Le"},
    {">=", "Ge"},       {"<", "Lt"},         {">", "Gt"},          {"+", "Plus"},
    {"-", "Minus"},     {"*", "Mul"},        {"/", "Div"},         {"%", "Mod"},
    {"++", "Inc"},      {"--", "Dec"},       {"+=", "AddAssign"},  {"-=", "SubAssign"},
    {"*=", "MulAssign"}, {"/=", "DivAssign"}, {"%=", "ModAssign"}, {"&=", "AndAssign"},
    {"|=", "OrAssign"}, {"^=", "XorAssign"}, {"<<=", "ShlAssign"}, {">>=", "ShrAssign"},
    {"&", "BitAnd"},    {"|", "BitOr"},      {"^", "BitXor"},      {"~", "BitNot"},
    {"<<", "Shl"},      {">>", "Shr"},       {"!", "Not"},         {"&&", "And"},
    {"||", "Or"},       {"=", "Assign"},     {"[]", "Index"},      {"()", "Call"},
    {"->", "Arrow"},    {"->*", "ArrowStar"}, {",", "Comma"},
};

}  // namespace

bool is_identifier(std::string_view text) {
    return !text.empty() && is_ident_start(text[0]) &&
           std::all_of(text.begin(), text.end(), is_ident_char);
}

bool is_qualified_name(std::string_view text, std::size_t min_parts) {
    std::size_t parts = 0;
    while (true) {
        const std::size_t sep = text.find("::");
        if (!is_identifier(text.substr(0, sep))) {
            return false;
        }
        ++parts;
        if (sep == std::string_view::npos) {
            return parts >= min_parts;
        }
        text.remove_prefix(sep + 2);
    }
}

std::string snake_case(std::string_view name) {
    std::string out;
    bool pending_separator = false;

    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        const Cls cls = classify(c);
        if (cls == Cls::Sep) {
            pending_separator = true;
            continue;
        }

        bool boundary = pending_separator;
        if (!boundary && i > 0 && cls == Cls::Upper) {
            const Cls prev = classify(name[i - 1]);
            if (prev == Cls::Lower || prev == Cls::Digit) {
                boundary = true;                                     // getHTTP, Vec3D
            } else if (prev == Cls::Upper && i + 1 < name.size() &&
                       classify(name[i + 1]) == Cls::Lower) {
                boundary = true;                                     // HTTPServer
            }
        }
        if (boundary && !out.empty()) {
            out += '_';
        }
        out += to_lower(c);
        pending_separator = false;
    }
    return out.empty() ? "unnamed" : out;
}

std::string file_stem(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash != std::string_view::npos) {
        path.remove_prefix(slash + 1);
    }
    const std::size_t dot = path.find_last_of('.');
    if (dot != std::string_view::npos && dot != 0) {
        path = path.substr(0, dot);
    }
    return std::string(path);
}

std::string match_key(std::string_view name) {
    std::string out;
    for (char c : name) {
        if (c != '_' && c != '-') {
            out += to_lower(c);
        }
    }
    return out;
}

std::string include_guard(std::string_view include_path) {
    std::string out;
    bool pending_separator = false;
    for (char c : include_path) {
        if (is_upper(c) || is_lower(c) || is_digit(c)) {
            if (pending_separator && !out.empty()) {
                out += '_';
            }
            pending_separator = false;
            out += to_upper(c);
        } else {
            pending_separator = true;
        }
    }
    if (out.empty()) {
        return "MT_INCLUDE_GUARD";
    }
    if (is_digit(out.front())) {
        out.insert(0, "H_");
    }
    return out;
}

std::string expand_pattern(std::string_view pattern, std::string_view stem,
                           std::string_view class_name) {
    std::string out;
    for (std::size_t i = 0; i < pattern.size();) {
        if (pattern.compare(i, 6, "{stem}") == 0) {
            out += stem;
            i += 6;
        } else if (pattern.compare(i, 7, "{Class}") == 0) {
            out += class_name;
            i += 7;
        } else {
            out += pattern[i++];
        }
    }
    return out;
}

std::string test_base_name(std::string_view method_name) {
    constexpr std::string_view kKeyword = "operator";
    if (method_name.size() <= kKeyword.size() || method_name.substr(0, kKeyword.size()) != kKeyword ||
        is_ident_char(method_name[kKeyword.size()])) {
        return std::string(method_name);   // not an operator (or "operatorX")
    }

    const std::string rest = trim(method_name.substr(kKeyword.size()));
    if (rest.empty()) {
        return std::string(method_name);
    }

    if (!is_ident_start(rest.front())) {
        const std::string compact = without_spaces(rest);
        if (compact.rfind("\"\"", 0) == 0) {
            return "OperatorLiteral" + camel_type(compact.substr(2));   // operator""_km
        }
        for (const OperatorName& op : kOperators) {
            if (compact == op.symbol) {
                return std::string("Operator") + op.name;
            }
        }
        return "OperatorUnknown";
    }

    const std::string compact = without_spaces(rest);
    if (compact == "new")      return "OperatorNew";
    if (compact == "new[]")    return "OperatorNewArray";
    if (compact == "delete")   return "OperatorDelete";
    if (compact == "delete[]") return "OperatorDeleteArray";
    return "ConversionTo" + camel_type(rest);
}

std::string type_suffix(const std::vector<std::string>& param_types) {
    std::vector<std::string> words;
    for (const std::string& type : param_types) {
        for (std::size_t i = 0; i < type.size();) {
            if (is_ident_char(type[i])) {
                std::size_t j = i;
                while (j < type.size() && is_ident_char(type[j])) ++j;
                const std::string token = type.substr(i, j - i);
                if (token != "const" && token != "volatile") {
                    words.push_back(snake_case(token));
                }
                i = j;
            } else {
                if (type[i] == '*') {
                    words.push_back("ptr");
                }
                ++i;   // '&' and everything else only separate words
            }
        }
    }
    std::string out;
    for (const std::string& w : words) {
        if (!out.empty()) out += '_';
        out += w;
    }
    return out;
}

std::vector<std::string> unique_test_names(const std::vector<TestNameInput>& inputs) {
    std::map<std::string, std::size_t> uses;
    for (const TestNameInput& in : inputs) {
        ++uses[in.base];
    }

    std::vector<std::string> names;
    names.reserve(inputs.size());
    std::set<std::string> taken;
    for (const TestNameInput& in : inputs) {
        std::string wanted = in.base;
        if (uses[in.base] > 1) {
            const std::string suffix = type_suffix(in.param_types);
            if (!suffix.empty()) {
                wanted += "_" + suffix;
            }
        }

        std::string candidate = wanted;
        for (unsigned n = 2; taken.count(candidate) != 0; ++n) {
            candidate = wanted + "_" + std::to_string(n);
        }
        taken.insert(candidate);
        names.push_back(std::move(candidate));
    }
    return names;
}

bool is_qtest_special_name(std::string_view name) {
    if (name == "init" || name == "cleanup" || name == "initTestCase" || name == "cleanupTestCase") return true;
    return name.size() > 5 && name.compare(name.size() - 5, 5, "_data") == 0;
}

}  // namespace mt::core
