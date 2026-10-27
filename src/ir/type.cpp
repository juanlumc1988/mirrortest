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

namespace {

bool ends_with(const std::string& s, std::string_view tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// `const` as a word at the end: "Item const" yes, "Item_const" no.
bool ends_with_word(const std::string& s, std::string_view word) {
    if (!ends_with(s, word)) return false;
    const std::size_t before = s.size() - word.size();
    if (before == 0) return true;
    const char c = s[before - 1];
    return !(std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_');
}

bool starts_with_word(const std::string& s, std::string_view word) {
    if (s.compare(0, word.size(), word) != 0) return false;
    if (s.size() == word.size()) return true;
    const char c = s[word.size()];
    return !(std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_');
}

void rtrim(std::string& s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
}

// The index of the bracket that closes the one at `open` (one of ( [ {), or the last index if nothing does.
std::size_t closing_bracket(const std::string& s, std::size_t open) {
    int depth = 0;
    for (std::size_t i = open; i < s.size(); ++i) {
        if (s[i] == '(' || s[i] == '[' || s[i] == '{') ++depth;
        if ((s[i] == ')' || s[i] == ']' || s[i] == '}') && --depth == 0) return i;
    }
    return s.size() - 1;
}

// Whether the word that ends just before `at` is one whose parentheses hold an expression and are part of the type.
bool follows_type_expression_keyword(const std::string& s, std::size_t at) {
    std::size_t end = at;
    while (end > 0 && s[end - 1] == ' ') --end;
    std::size_t begin = end;
    while (begin > 0 && (std::isalnum(static_cast<unsigned char>(s[begin - 1])) != 0 || s[begin - 1] == '_')) --begin;
    const std::string word = s.substr(begin, end - begin);
    return word == "decltype" || word == "typeof" || word == "__typeof__" || word == "__typeof";
}

// Whether a '(' or '[' stands outside every pair of angle brackets and is not the argument of a decltype or a typeof:
// the spelling is then a function pointer, an array or a reference to one, which is kept whole. Parentheses inside
// angle brackets (`std::function<void(int)>`) and the argument of a decltype belong to a name, not to a declarator.
bool has_declarator_brackets(const std::string& s) {
    int angles = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '<') {
            ++angles;
        } else if (c == '>' && angles > 0) {
            --angles;
        } else if (c == '(' || c == '[') {
            if (angles > 0 || (c == '(' && follows_type_expression_keyword(s, i))) {
                i = closing_bracket(s, i);
                continue;
            }
            return true;
        }
    }
    return false;
}

}  // namespace

Type type_from_spelling(std::string_view spelling) {
    Type t;
    t.spelling = normalize_spelling(spelling);
    if (t.spelling.empty()) return t;
    if (has_declarator_brackets(t.spelling)) {
        t.base = t.spelling;       // a function pointer, an array: exact, and nothing finer
        return t;
    }

    std::string s = t.spelling;
    if (ends_with(s, "&&")) {
        t.ref = RefKind::RValue;
        s.resize(s.size() - 2);
    } else if (ends_with(s, "&")) {
        t.ref = RefKind::LValue;
        s.pop_back();
    }

    // From the right: stars and consts. A const read before the next star
    // belongs to that pointer ("char* const") and is not represented; one left
    // when no star remains belongs to the base ("Item const").
    bool const_pending = false;
    for (;;) {
        rtrim(s);
        if (ends_with(s, "*")) {
            s.pop_back();
            ++t.pointer_depth;
            const_pending = false;
        } else if (ends_with_word(s, "const")) {
            s.resize(s.size() - 5);
            const_pending = true;
        } else if (ends_with_word(s, "volatile")) {
            s.resize(s.size() - 8);
        } else {
            break;
        }
    }
    bool leading_const = false;
    while (true) {
        if (starts_with_word(s, "const")) {
            s.erase(0, 5);
            leading_const = true;
        } else if (starts_with_word(s, "volatile")) {
            s.erase(0, 8);
        } else {
            break;
        }
        while (!s.empty() && s[0] == ' ') s.erase(0, 1);
    }
    rtrim(s);
    t.base = s;
    t.is_const = leading_const || const_pending;
    return t;
}

std::string collapse_spaces(std::string_view text) {
    std::string out;
    bool pending = false;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            pending = !out.empty();
        } else {
            if (pending) out += ' ';
            pending = false;
            out += c;
        }
    }
    return out;
}

bool is_operator_name(const std::string& name) {
    constexpr std::string_view keyword = "operator";
    if (name.size() <= keyword.size() || name.compare(0, keyword.size(), keyword) != 0) {
        return false;
    }
    const char next = name[keyword.size()];
    const bool continues_identifier = (next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') ||
                                      (next >= '0' && next <= '9') || next == '_';
    return !continues_identifier;
}

std::string normalize_operator_name(const std::string& name) {
    if (!is_operator_name(name)) {
        return name;
    }
    const std::string rest = collapse_spaces(std::string_view(name).substr(8));
    if (rest.empty()) {
        return name;
    }
    const char first = rest[0];
    const bool word = (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_';
    if (word) {
        std::string tidy;
        for (std::size_t i = 0; i < rest.size(); ++i) {
            if (rest[i] == ' ' && ((i + 1 < rest.size() && (rest[i + 1] == '[' || rest[i + 1] == ']')) || (i > 0 && rest[i - 1] == '['))) continue;
            tidy += rest[i];
        }
        return "operator " + tidy;
    }
    std::string symbol;
    for (char c : rest) {
        if (c != ' ') symbol += c;
    }
    return "operator" + symbol;
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
