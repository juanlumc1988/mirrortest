// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/syntax/mock_macros.h"

#include <cstddef>
#include <string>
#include <vector>

namespace mt::syntax {

namespace {

bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }
bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

bool starts_with(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

// One space for every run of white space, line breaks included: the declaration is one line.
std::string squeeze(std::string_view s) {
    std::string out;
    bool pending = false;
    for (char c : trim(s)) {
        if (is_space(c)) {
            pending = true;
            continue;
        }
        if (pending) out += ' ';
        pending = false;
        out += c;
    }
    return out;
}

// The parts of `text` between the commas that are not inside parentheses. The
// angle brackets are not counted: in a macro's arguments a template's commas
// are already protected by parentheses, or the macro would not compile.
std::vector<std::string_view> split_arguments(std::string_view text) {
    std::vector<std::string_view> out;
    int depth = 0;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        else if (c == ',' && depth == 0) {
            out.push_back(trim(text.substr(begin, i - begin)));
            begin = i + 1;
        }
    }
    out.push_back(trim(text.substr(begin)));
    return out;
}

// "(a, b)" -> "a, b" when the parentheses are the whole of the text and enclose it.
bool unwrap(std::string_view& text) {
    text = trim(text);
    if (text.size() < 2 || text.front() != '(' || text.back() != ')') return false;
    int depth = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '(') ++depth;
        else if (text[i] == ')' && --depth == 0 && i + 1 != text.size()) return false;   // closes before the end: "(a) (b)"
    }
    if (depth != 0) return false;
    text = trim(text.substr(1, text.size() - 2));
    return true;
}

// A type that was parenthesised to protect its commas, or not at all.
std::string plain_type(std::string_view text) {
    text = trim(text);
    std::string_view inner = text;
    if (unwrap(inner)) return squeeze(inner);
    return squeeze(text);
}

bool valid_method_name(std::string_view name) {
    if (name.empty()) return false;
    if (starts_with(name, "operator")) return true;
    if (!is_ident_start(name.front())) return false;
    for (char c : name) {
        if (!is_ident_char(c)) return false;
    }
    return true;
}

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i == 0 ? "" : ", ") + parts[i];
    return out;
}

struct Specs {
    bool is_const = false;
    bool is_noexcept = false;
    bool is_override = false;
    bool is_final = false;
    std::string ref;   // "&", "&&" or ""
};

Specs read_specs(std::string_view text) {
    Specs specs;
    std::string_view inner = text;
    if (!unwrap(inner)) return specs;
    for (std::string_view word : split_arguments(inner)) {
        if (word == "const") specs.is_const = true;
        else if (word == "noexcept") specs.is_noexcept = true;
        else if (word == "override") specs.is_override = true;
        else if (word == "final") specs.is_final = true;
        else if (starts_with(word, "ref(") && word.back() == ')') specs.ref = squeeze(word.substr(4, word.size() - 5));
        // Calltype(...) is the calling convention of a platform that has one; nothing here depends on it.
    }
    return specs;
}

std::string with_specs(std::string declaration, const Specs& specs) {
    if (specs.is_const) declaration += " const";
    if (!specs.ref.empty()) declaration += " " + specs.ref;
    if (specs.is_noexcept) declaration += " noexcept";
    if (specs.is_override) declaration += " override";
    if (specs.is_final) declaration += " final";
    return declaration;
}

// MOCK_METHOD(Ret, Name, (Args), (Specs)).
std::string new_style(std::string_view arguments) {
    const std::vector<std::string_view> parts = split_arguments(arguments);
    if (parts.size() < 3 || parts.size() > 4) return {};
    const std::string ret = plain_type(parts[0]);
    const std::string name = squeeze(parts[1]);
    std::string_view list = parts[2];
    if (ret.empty() || !valid_method_name(name) || !unwrap(list)) return {};

    std::vector<std::string> args;
    if (!list.empty()) {
        for (std::string_view arg : split_arguments(list)) {
            if (arg.empty()) return {};
            args.push_back(plain_type(arg));
        }
    }
    const Specs specs = parts.size() == 4 ? read_specs(parts[3]) : Specs{};
    return with_specs(ret + " " + name + "(" + join(args) + ")", specs);
}

// MOCK_METHODn(Name, Ret(Args)), MOCK_CONST_METHODn, and the _WITH_CALLTYPE forms (a calling convention first).
std::string old_style(bool is_const, bool with_calltype, std::string_view arguments) {
    const std::vector<std::string_view> parts = split_arguments(arguments);
    const std::size_t first = with_calltype ? 1 : 0;
    if (parts.size() != first + 2) return {};
    const std::string name = squeeze(parts[first]);
    std::string_view function = parts[first + 1];
    if (!valid_method_name(name) || function.empty() || function.back() != ')') return {};

    // Ret(Args): the parenthesis that closes the text opens the arguments.
    int depth = 0;
    std::size_t open = std::string_view::npos;
    for (std::size_t i = function.size(); i-- > 0;) {
        if (function[i] == ')') ++depth;
        else if (function[i] == '(' && --depth == 0) {
            open = i;
            break;
        }
    }
    if (open == std::string_view::npos || open == 0) return {};
    const std::string ret = squeeze(function.substr(0, open));
    const std::string_view list = function.substr(open + 1, function.size() - open - 2);
    if (ret.empty()) return {};

    std::vector<std::string> args;
    if (!trim(list).empty()) {
        for (std::string_view arg : split_arguments(list)) {
            if (arg.empty()) return {};
            args.push_back(squeeze(arg));
        }
    }
    Specs specs;
    specs.is_const = is_const;
    specs.is_override = true;
    return with_specs(ret + " " + name + "(" + join(args) + ")", specs);
}

}  // namespace

bool is_mock_method_macro(std::string_view word) {
    if (word == "MOCK_METHOD") return true;
    std::string_view rest = word;
    if (starts_with(rest, "MOCK_CONST_METHOD")) rest.remove_prefix(17);
    else if (starts_with(rest, "MOCK_METHOD")) rest.remove_prefix(11);
    else return false;
    std::size_t digits = 0;
    while (digits < rest.size() && rest[digits] >= '0' && rest[digits] <= '9') ++digits;
    if (digits == 0 || digits > 2) return false;
    rest.remove_prefix(digits);
    return rest.empty() || rest == "_T" || rest == "_WITH_CALLTYPE" || rest == "_T_WITH_CALLTYPE";
}

std::string mock_method_declaration(std::string_view word, std::string_view arguments) {
    if (!is_mock_method_macro(word)) return {};
    if (word == "MOCK_METHOD") return new_style(arguments);
    const bool is_const = starts_with(word, "MOCK_CONST_METHOD");
    const bool with_calltype = word.size() >= 14 && word.substr(word.size() - 14) == "_WITH_CALLTYPE";
    return old_style(is_const, with_calltype, arguments);
}

}  // namespace mt::syntax
