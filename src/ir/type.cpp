// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/ir/type.h"

#include <cctype>

namespace mt::ir {

namespace {

bool is_word_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_';
}

bool is_word(const std::string& token) {
    return !token.empty() && is_word_char(token.front());
}

// Words after which a following "::" starts a new name rather than
// continuing a qualified one: "const ::ns::T", not "const::ns::T".
bool is_keyword(const std::string& token) {
    static const char* const keywords[] = {
        "const", "volatile", "typename", "struct", "class", "enum", "union",
        "unsigned", "signed", "long", "short",
    };
    for (const char* k : keywords) {
        if (token == k) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> tokenize(std::string_view s) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            ++i;
        } else if (is_word_char(c)) {
            std::size_t j = i;
            while (j < s.size() && is_word_char(s[j])) {
                ++j;
            }
            tokens.emplace_back(s.substr(i, j - i));
            i = j;
        } else if ((c == ':' || c == '&') && i + 1 < s.size() && s[i + 1] == c) {
            tokens.emplace_back(s.substr(i, 2));
            i += 2;
        } else {
            tokens.emplace_back(1, c);
            ++i;
        }
    }
    return tokens;
}

bool needs_space(const std::string& prev, const std::string& next) {
    if (is_word(prev) && is_word(next)) {
        return true;                        // "unsigned long"
    }
    if (prev == ",") {
        return true;                        // "std::map<int, int>"
    }
    if ((prev == "*" || prev == "&" || prev == "&&") && is_word(next)) {
        return true;                        // "int* const"
    }
    if (next == "::" && is_keyword(prev)) {
        return true;                        // "const ::ns::T"
    }
    return false;
}

}  // namespace

std::string normalize_spelling(std::string_view spelling) {
    const std::vector<std::string> tokens = tokenize(spelling);
    std::string out;
    out.reserve(spelling.size());
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0 && needs_space(tokens[i - 1], tokens[i])) {
            out += ' ';
        }
        out += tokens[i];
    }
    return out;
}

Type make_type(std::string_view base, bool is_const, RefKind ref, unsigned pointer_depth) {
    Type t;
    t.base = normalize_spelling(base);
    t.is_const = is_const;
    t.ref = ref;
    t.pointer_depth = pointer_depth;

    std::string s;
    if (is_const) {
        s += "const ";
    }
    s += t.base;
    s.append(pointer_depth, '*');
    if (ref == RefKind::LValue) {
        s += '&';
    } else if (ref == RefKind::RValue) {
        s += "&&";
    }
    t.spelling = normalize_spelling(s);
    return t;
}

std::string qualified_name(const std::vector<std::string>& ns, std::string_view name) {
    std::string out;
    for (const std::string& part : ns) {
        out += part;
        out += "::";
    }
    out += name;
    return out;
}

}  // namespace mt::ir
