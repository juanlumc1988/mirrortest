// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/frontend/clang_frontend.h"

#include <string>
#include <utility>
#include <vector>

#ifndef MT_WITH_LIBCLANG
#define MT_WITH_LIBCLANG 0
#endif

#if MT_WITH_LIBCLANG
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <optional>

#include <clang-c/Index.h>

#include "mirrortest/ir/type.h"
#endif

namespace mt::frontend {

bool libclang_supported() { return MT_WITH_LIBCLANG != 0; }

#if MT_WITH_LIBCLANG

namespace {

using syntax::QtMarker;

constexpr unsigned kMaxParseWarnings = 10;

std::string to_string(CXString s) {
    const char* c = clang_getCString(s);
    std::string out = c != nullptr ? c : "";
    clang_disposeString(s);
    return out;
}

// One token of the source, with where it is in the text libclang was given.
struct Tok {
    std::string text;
    unsigned    begin = 0;
    unsigned    end = 0;
};

struct Span {
    unsigned begin = 0;
    unsigned end = 0;
};

ir::Access to_access(CX_CXXAccessSpecifier a, ir::Access otherwise) {
    switch (a) {
        case CX_CXXPublic:    return ir::Access::Public;
        case CX_CXXProtected: return ir::Access::Protected;
        case CX_CXXPrivate:   return ir::Access::Private;
        case CX_CXXInvalidAccessSpecifier: break;
    }
    return otherwise;
}

bool is_class_kind(CXCursorKind k) { return k == CXCursor_ClassDecl || k == CXCursor_StructDecl; }

bool is_method_kind(CXCursorKind k) {
    return k == CXCursor_CXXMethod || k == CXCursor_Constructor || k == CXCursor_Destructor || k == CXCursor_ConversionFunction;
}

// `struct` or `struct Name`, before the brace that opens its body: the previous two tokens.
bool is_tag(const std::string& one_before, const std::string& two_before) {
    const auto keyword = [](const std::string& x) { return x == "struct" || x == "class" || x == "union" || x == "enum"; };
    return keyword(one_before) || keyword(two_before);
}

bool is_unnamed(const std::string& name) {
    return name.empty() || name.find("(unnamed") != std::string::npos || name.find("(anonymous") != std::string::npos;
}

// What the tokens of a declaration say beyond what the libclang API does: the keywords that
// are not in its types. Read from the text, as the other frontend does from its tree, so that
// "declared virtual" means written `virtual`, not "overrides something".
struct Shape {
    bool is_static = false;
    bool is_virtual = false;
    bool is_explicit = false;
    bool is_const = false;
    bool is_override = false;
    bool is_final = false;
    bool is_noexcept = false;
    bool is_pure = false;
    bool is_deleted = false;
    bool is_defaulted = false;
    ir::RefKind ref = ir::RefKind::None;
    std::optional<std::pair<std::size_t, std::size_t>> trailing_return;   // the tokens after `->` (first, last), if there are any
};

bool is_specifier(const std::string& x) {
    static const char* const words[] = {"static",    "virtual",  "inline",  "constexpr", "consteval",   "constinit", "explicit",
                                        "friend",    "extern",   "mutable", "register",  "thread_local", "typedef",   "__inline",
                                        "__extension__"};
    return std::any_of(std::begin(words), std::end(words), [&](const char* w) { return x == w; });
}

std::string compact(const std::vector<Tok>& tokens, std::size_t from, std::size_t to) {
    std::string out;
    for (std::size_t i = from; i < to; ++i) out += tokens[i].text;
    return out;
}

// The index of the ')' that closes the first '(' at or after `from`, or the end if there is none.
std::size_t closing(const std::vector<Tok>& t, std::size_t from) {
    int depth = 0;
    for (std::size_t i = from; i < t.size(); ++i) {
        if (t[i].text == "(") ++depth;
        if (t[i].text == ")" && --depth == 0) return i;
    }
    return t.size();
}

// Where the parameter list of a function's declaration is, in the offsets of its '(' and its ')'.
struct ParameterList {
    bool     found = false;
    unsigned open = 0;
    unsigned close = 0;
    bool     c_variadic = false;   // it ends in a C-style `...`, which is not the `...` of a pack
};

// The parameter list is the first '(' after the name that is not the pair of `operator()` and is not inside the angle
// brackets of the type a conversion function converts to (`operator Action<U(Args...)>()`).
ParameterList find_parameter_list(const std::vector<Tok>& t, unsigned name_offset) {
    ParameterList list;
    std::size_t i = 0;
    while (i < t.size() && t[i].begin < name_offset) ++i;
    if (i < t.size() && t[i].text == "operator") {
        ++i;
        if (i + 1 < t.size() && t[i].text == "(" && t[i + 1].text == ")") {
            i += 2;
        } else {
            int angles = 0;
            for (; i < t.size(); ++i) {
                if (t[i].text == "<") ++angles;
                if (t[i].text == ">") --angles;
                if (t[i].text == ">>") angles -= 2;
                if (t[i].text == "(" && angles <= 0) break;
            }
        }
    }
    const std::size_t close = closing(t, i);
    if (close >= t.size()) return list;
    while (t[i].text != "(") ++i;
    list.found = true;
    list.open = t[i].begin;
    list.close = t[close].begin;
    list.c_variadic = close >= i + 2 && t[close - 1].text == "..." && (t[close - 2].text == "(" || t[close - 2].text == ",");
    return list;
}

// Whether the tokens of a function's declaration include its parameter list.
bool has_parameter_list(const std::vector<Tok>& t, unsigned name_offset) { return find_parameter_list(t, name_offset).found; }

Shape read_shape(const std::vector<Tok>& t, unsigned name_offset) {
    Shape s;
    std::size_t i = 0;
    for (; i < t.size() && t[i].begin < name_offset; ++i) {
        if (t[i].text == "static") s.is_static = true;
        if (t[i].text == "virtual") s.is_virtual = true;
        if (t[i].text == "explicit") s.is_explicit = true;
    }
    // The parameter list is the first parentheses from the name on (`operator()` has parentheses of its own, which
    // close at once and leave the real ones to be skipped below like any other).
    const std::size_t close = closing(t, i);

    int depth = 0;
    for (std::size_t k = close + 1; k < t.size(); ++k) {
        const std::string& x = t[k].text;
        if (depth == 0 && x == "->") {
            // A trailing return type runs to what ends it: the `;` or `{`, the `= 0` or `= delete`, `override`, `final`.
            // Parentheses keep what is inside them (`decltype(a = b)`) from ending it. What follows is read like the rest.
            std::size_t last = k + 1;
            int inner = 0;
            while (last < t.size()) {
                const std::string& y = t[last].text;
                if (y == "(") ++inner;
                if (y == ")") --inner;
                if (inner <= 0 && (y == ";" || y == "{" || y == "=" || y == "override" || y == "final" || y == "requires")) break;
                ++last;
            }
            if (last > k + 1) s.trailing_return = std::make_pair(k + 1, last - 1);
            k = last - 1;
            continue;
        }
        if (depth == 0 && (x == ";" || x == "{" || x == ":" || x == "try" || x == "requires")) break;   // and nothing after a requires-clause is a qualifier
        if (x == "(") ++depth;
        if (x == ")") --depth;
        if (depth != 0) continue;
        if (x == "const") {
            s.is_const = true;
        } else if (x == "override") {
            s.is_override = true;
        } else if (x == "final") {
            s.is_final = true;
        } else if (x == "&") {
            s.ref = ir::RefKind::LValue;
        } else if (x == "&&") {
            s.ref = ir::RefKind::RValue;
        } else if (x == "noexcept") {
            if (k + 1 < t.size() && t[k + 1].text == "(") {
                const std::size_t end = closing(t, k + 1);
                s.is_noexcept = compact(t, k + 2, end) == "true";   // noexcept(expr) keeps false
                k = end;
            } else {
                s.is_noexcept = true;
            }
        } else if (x == "=" && k + 1 < t.size()) {
            const std::string& v = t[k + 1].text;
            if (v == "0") s.is_pure = true;
            if (v == "delete") s.is_deleted = true;
            if (v == "default") s.is_defaulted = true;
        }
    }
    return s;
}

class Reader {
public:
    Reader(CXTranslationUnit tu, const std::string& text, const std::vector<QtMarker>& markers, const std::string& main_file,
           const std::string& file, const std::string& header, ir::Model& model, core::Diagnostics& diagnostics)
        : tu_(tu), text_(text), markers_(markers), main_file_(main_file), file_(file), header_(header), model_(model),
          diagnostics_(diagnostics) {}

    void run() {
        report_diagnostics();
        for (CXCursor c : children(clang_getTranslationUnitCursor(tu_))) {
            if (clang_getCursorKind(c) == CXCursor_MacroExpansion && in_main_file(c)) macro_expansions_.push_back(span_of(c).begin);
        }
        std::sort(macro_expansions_.begin(), macro_expansions_.end());
        declarations(clang_getTranslationUnitCursor(tu_));
    }

private:
    struct ClassState {
        ir::Class& cls;
        ir::QtRole section = ir::QtRole::None;
        unsigned   previous_end = 0;
    };

    // ---- the text and the cursors --------------------------------------------------------------

    static std::vector<CXCursor> children(CXCursor parent) {
        std::vector<CXCursor> out;
        clang_visitChildren(
            parent,
            [](CXCursor c, CXCursor, CXClientData data) {
                static_cast<std::vector<CXCursor>*>(data)->push_back(c);
                return CXChildVisit_Continue;
            },
            &out);
        return out;
    }

    static unsigned offset_of(CXSourceLocation where) {
        unsigned offset = 0;
        clang_getExpansionLocation(where, nullptr, nullptr, nullptr, &offset);
        return offset;
    }

    static Span span_of(CXCursor c) {
        const CXSourceRange r = clang_getCursorExtent(c);
        return {offset_of(clang_getRangeStart(r)), offset_of(clang_getRangeEnd(r))};
    }

    static unsigned name_offset(CXCursor c) { return offset_of(clang_getCursorLocation(c)); }

    static bool in_main_file(CXCursor c) { return clang_Location_isFromMainFile(clang_getCursorLocation(c)) != 0; }

    ir::SourceLoc location_of(CXSourceLocation where) const {
        unsigned line = 0;
        unsigned column = 0;
        clang_getExpansionLocation(where, nullptr, &line, &column, nullptr);
        ir::SourceLoc loc;
        loc.file = file_;
        loc.line = line;
        loc.column = column;
        return loc;
    }

    ir::SourceLoc location(CXCursor c) const { return location_of(clang_getCursorLocation(c)); }

    ir::SourceLoc location_at(unsigned offset) const {
        // The line and column of an offset of the text, for a thing no cursor stands for.
        ir::SourceLoc loc;
        loc.file = file_;
        loc.line = 1;
        loc.column = 1;
        for (unsigned i = 0; i < offset && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++loc.line;
                loc.column = 1;
            } else {
                ++loc.column;
            }
        }
        return loc;
    }

    std::string slice(unsigned begin, unsigned end) const {
        if (begin >= end || begin >= text_.size()) return {};
        return text_.substr(begin, std::min<std::size_t>(end, text_.size()) - begin);
    }

    // Whether the first thing on its line before `offset` is a '#': the offset is where a preprocessor line starts.
    bool starts_a_directive(unsigned offset) const {
        std::size_t i = offset;
        while (i > 0 && (text_[i - 1] == ' ' || text_[i - 1] == '\t')) --i;
        return (i == 0 || text_[i - 1] == '\n') && offset < text_.size() && text_[offset] == '#';
    }

    // The end of the preprocessor line that starts at `offset`: the first line break that is not escaped.
    unsigned directive_end(unsigned offset) const {
        std::size_t i = offset;
        while (i < text_.size() && !(text_[i] == '\n' && (i == 0 || text_[i - 1] != '\\'))) ++i;
        return static_cast<unsigned>(i);
    }

    // The tokens of the text between two offsets, comments and preprocessor lines left out (a declaration can have an
    // `#ifdef` in the middle of an expression of its own).
    std::vector<Tok> tokens_in(CXSourceRange range, unsigned limit) const {
        std::vector<Tok> out;
        CXToken* raw = nullptr;
        unsigned count = 0;
        clang_tokenize(tu_, range, &raw, &count);
        unsigned directive_until = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (clang_getTokenKind(raw[i]) == CXToken_Comment) continue;
            const CXSourceRange r = clang_getTokenExtent(tu_, raw[i]);
            Tok t;
            t.begin = offset_of(clang_getRangeStart(r));
            t.end = offset_of(clang_getRangeEnd(r));
            if (t.begin >= limit) continue;   // some versions return one token past the range
            if (t.begin < directive_until) continue;
            t.text = to_string(clang_getTokenSpelling(tu_, raw[i]));
            if (t.text == "#" && starts_a_directive(t.begin)) {
                directive_until = directive_end(t.begin);
                continue;
            }
            out.push_back(std::move(t));
        }
        clang_disposeTokens(tu_, raw, count);
        return out;
    }

    // Tokens `first` to `last` (inclusive) as they were written: the text between them as it is, except where it holds
    // more than blanks (a comment, a preprocessor line in the middle of an expression), which becomes one space.
    std::string written(const std::vector<Tok>& t, std::size_t first, std::size_t last) const {
        if (first > last || last >= t.size()) return {};
        std::string out = slice(t[first].begin, t[first].end);
        for (std::size_t i = first + 1; i <= last; ++i) {
            const std::string gap = slice(t[i - 1].end, t[i].begin);
            const bool blank = std::all_of(gap.begin(), gap.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });
            out += blank ? gap : std::string(" ");
            out += slice(t[i].begin, t[i].end);
        }
        return out;
    }

    std::vector<Tok> tokens(CXCursor c) const { return tokens_in(clang_getCursorExtent(c), span_of(c).end); }

    // The tokens of a function's declaration. libclang's extent of `auto f() -> decltype(x);` is the two words
    // `auto f`: the trailing return type that has a decltype in it is left out, and so is everything after it (the
    // parameter list, `const`, `override`). A declaration whose extent has no parameter list (the `()` of an
    // `operator()` is not one) is read on, from the same start, to the `;` or `{` that ends it.
    std::vector<Tok> function_tokens(CXCursor c) const {
        std::vector<Tok> t = tokens(c);
        if (has_parameter_list(t, name_offset(c))) return t;
        const CXFile file = clang_getFile(tu_, main_file_.c_str());
        if (file == nullptr) return t;
        const CXSourceRange rest = clang_getRange(clang_getRangeStart(clang_getCursorExtent(c)),
                                                  clang_getLocationForOffset(tu_, file, static_cast<unsigned>(text_.size())));
        std::vector<Tok> longer;
        int depth = 0;
        for (Tok& x : tokens_in(rest, static_cast<unsigned>(text_.size()))) {
            if (x.text == "(" || x.text == "[") ++depth;
            if (x.text == ")" || x.text == "]") --depth;
            if (depth == 0 && (x.text == ";" || x.text == "{")) return longer;
            longer.push_back(std::move(x));
        }
        return t;   // it never ends: whatever the extent had
    }

    // A declaration that comes out of a macro has no text of its own to read a type from: its first
    // token is written somewhere else than where it is expanded. (The location of the name is no help: for a
    // parameter written in the macro's body libclang reports the expansion for both.) Nor has one that starts with a macro
    // (`MYLIB_API int size() const;`): the macro may expand to an attribute, and then libclang tokenises the declaration
    // from the attribute in the definition of the macro, so the tokens are everything from there to its end. What the
    // macro expands to is for libclang to say, which is why an export macro needs no `ignore_macros`.
    bool from_macro(CXCursor c) const {
        const CXSourceLocation where = clang_getRangeStart(clang_getCursorExtent(c));
        unsigned spelled = 0;
        unsigned expanded = 0;
        clang_getSpellingLocation(where, nullptr, nullptr, nullptr, &spelled);
        clang_getExpansionLocation(where, nullptr, nullptr, nullptr, &expanded);
        return spelled != expanded || is_macro_use(expanded);
    }

    // Whether a macro is used at this offset of the text.
    bool is_macro_use(unsigned offset) const { return std::binary_search(macro_expansions_.begin(), macro_expansions_.end(), offset); }

    // Whether a macro is used in tokens [from, to): a type with a macro in it (`static YAML_CPP_API std::atomic<size_t> x;`)
    // is for libclang to spell, because the macro may stand for a word of the type or for nothing at all.
    bool has_macro_use(const std::vector<Tok>& t, std::size_t from, std::size_t to) const {
        for (std::size_t i = from; i < to && i < t.size(); ++i) {
            if (is_macro_use(t[i].begin)) return true;
        }
        return false;
    }

    ir::Type make(const std::string& spelling, CXSourceLocation where) const {
        ir::Type t = ir::type_from_spelling(spelling);
        t.origin = ir::Origin::Declared;
        t.where = location_of(where);
        return t;
    }

    // libclang's own spelling of a type: what the compiler makes of it, not what the user wrote. Used
    // where there is no text to read, because it follows the printing policy of the libclang at hand
    // (`unsigned` is `unsigned int`) and, for a type it does not know, says `int`.
    ir::Type type_of(CXType type, CXSourceLocation where) const {
        return make(ir::normalize_spelling(to_string(clang_getTypeSpelling(type))), where);
    }

    static std::size_t index_at(const std::vector<Tok>& t, unsigned offset) {
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (t[i].begin >= offset) return i;
        }
        return std::string::npos;
    }

    // Whether a parenthesis before the name (token `name_at`) opens a declarator, as in `void (*get())(int)`, and not a
    // decltype, an attribute or an alignas, whose parentheses are part of a type that can be read as written. (Whether
    // the C++11 attributes and alignas are among the tokens of a declaration depends on the version of libclang: the
    // 18 leaves them out of the extent, later ones keep them.)
    static bool has_declarator_parentheses(const std::vector<Tok>& t, std::size_t name_at) {
        for (std::size_t i = 0; i < name_at && i < t.size(); ++i) {
            const std::string& x = t[i].text;
            const bool own_parentheses = x == "decltype" || x == "typeof" || x == "__typeof__" || x == "__attribute__" || x == "alignas" || x == "__declspec";
            if (own_parentheses && i + 1 < t.size() && t[i + 1].text == "(") {
                i = closing(t, i + 1);
            } else if (x == "(") {
                return true;
            }
        }
        return false;
    }

    // The type that tokens [from, to) spell once the words that only say how something is declared
    // (static, virtual, inline, ...), the attributes, a template header and the token `skip` (the name) are
    // taken out. Whitespace is the normaliser's business.
    static std::string spelled(const std::vector<Tok>& t, std::size_t from, std::size_t to, std::size_t skip) {
        std::string out;
        to = std::min(to, t.size());
        for (std::size_t i = from; i < to; ++i) {
            const std::string& x = t[i].text;
            if (i == skip || is_specifier(x)) continue;
            if (x == "template" && i + 1 < to && t[i + 1].text == "<") {
                int depth = 0;
                for (++i; i < to; ++i) {
                    if (t[i].text == "<") ++depth;
                    if (t[i].text == ">") --depth;
                    if (t[i].text == ">>") depth -= 2;
                    if (depth <= 0) break;
                }
                continue;
            }
            if (x == "[" && i + 1 < to && t[i + 1].text == "[") {   // [[nodiscard]]
                while (i + 1 < to && !(t[i].text == "]" && t[i + 1].text == "]")) ++i;
                ++i;
                continue;
            }
            if ((x == "__attribute__" || x == "alignas" || x == "__declspec") && i + 1 < to && t[i + 1].text == "(") {
                i = closing(t, i + 1);
                continue;
            }
            out += out.empty() ? "" : " ";
            out += x;
        }
        return ir::normalize_spelling(out);
    }

    // For a declaration with several declarators, `int line, column;`: whether the tokens `t` are those of a declarator
    // after the first (`later`), whether the specifiers could be read (`read`: not when the first declarator is a function
    // pointer) and where they end (`end`): before the name of the first declarator, less the stars and references that
    // belong to it. `name_at` is the token that names the declarator the tokens are of.
    struct SharedSpecifiers {
        bool        later = false;
        bool        read = false;
        std::size_t end = 0;         // the specifiers are tokens [0, end)
        std::size_t own_start = 0;   // the declarator the tokens are of starts here (after the last comma before its name)
    };

    static SharedSpecifiers shared_specifiers(const std::vector<Tok>& t, std::size_t name_at) {
        SharedSpecifiers result;
        if (name_at == std::string::npos) return result;
        std::size_t comma = std::string::npos;   // the first comma, which ends the first declarator
        std::size_t last = std::string::npos;    // the last before the name, which starts this declarator
        int depth = 0;
        int angles = 0;
        for (std::size_t i = 0; i < name_at && i < t.size(); ++i) {
            const std::string& x = t[i].text;
            if (x == "(" || x == "[" || x == "{") ++depth;
            if (x == ")" || x == "]" || x == "}") --depth;
            if (depth == 0 && x == "<") ++angles;
            if (depth == 0 && x == ">" && angles > 0) --angles;
            if (depth == 0 && x == ">>" && angles > 0) angles = std::max(0, angles - 2);
            if (depth == 0 && angles == 0 && x == ",") {
                if (comma == std::string::npos) comma = i;
                last = i;
            }
        }
        if (comma == std::string::npos) return result;
        result.later = true;
        result.own_start = last + 1;
        // The first declarator ends at the comma; its name is the last identifier before whatever follows the name.
        std::size_t stop = comma;
        for (std::size_t i = 0; i < comma; ++i) {
            const std::string& x = t[i].text;
            if (x == "(") return result;                      // a function pointer, or a function: not read
            if (x == "=" || x == "{" || x == "[" || x == ":") {
                stop = i;
                break;
            }
        }
        std::size_t name = std::string::npos;
        for (std::size_t i = stop; i-- > 0;) {
            const char first = t[i].text.empty() ? ' ' : t[i].text[0];
            if (std::isalpha(static_cast<unsigned char>(first)) != 0 || first == '_') {
                name = i;
                break;
            }
        }
        if (name == std::string::npos || name == 0) return result;
        std::size_t end = name;
        while (end > 0) {
            const std::string& x = t[end - 1].text;
            const bool qualifier = (x == "const" || x == "volatile") && end >= 2 && (t[end - 2].text == "*" || t[end - 2].text == "&" || t[end - 2].text == "&&");
            if (x == "*" || x == "&" || x == "&&" || qualifier) --end;
            else break;
        }
        result.read = end > 0;
        result.end = end;
        return result;
    }

    // The type a parameter or a field declares, as written: its declaration without the name and
    // without what follows it (= initialiser, {initialiser}, : bits). A declaration libclang cannot
    // make sense of (a type it does not know) is still read this way, which is the point.
    ir::Type declared_type(CXCursor c) const {
        const CXSourceLocation where = clang_getRangeStart(clang_getCursorExtent(c));
        if (from_macro(c)) return type_of(clang_getCursorType(c), where);
        const std::vector<Tok> t = tokens(c);
        std::size_t skip = std::string::npos;
        if (!to_string(clang_getCursorSpelling(c)).empty()) skip = index_at(t, name_offset(c));
        std::size_t end = t.size();
        int depth = 0;
        for (std::size_t i = 0; i < t.size(); ++i) {
            const std::string& x = t[i].text;
            if (x == "(" || x == "[") ++depth;
            if (x == ")" || x == "]") --depth;
            if (depth == 0 && x == "{" && i > 0 && is_tag(t[i - 1].text, i > 1 ? t[i - 2].text : std::string())) {
                // `struct { int x; } inner;` and `struct Tag { int x; } inner;`: the braces are part of the type.
                int braces = 0;
                for (; i < t.size(); ++i) {
                    if (t[i].text == "{") ++braces;
                    if (t[i].text == "}" && --braces == 0) break;
                }
                continue;
            }
            if (depth == 0 && (x == "=" || x == "{" || x == ":")) {
                end = i;
                break;
            }
        }
        // `int line, column;` and `unsigned char *p, q, **r[2];`: the cursor of the second starts where the whole
        // declaration does, so its tokens hold the declarators before it. Its type is the specifiers, then what is
        // written around its own name: the stars before it, the array after it.
        const SharedSpecifiers shared = shared_specifiers(t, skip);
        if (shared.later) {
            if (!shared.read) return type_of(clang_getCursorType(c), where);
            std::vector<Tok> own(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(shared.end));
            own.insert(own.end(), t.begin() + static_cast<std::ptrdiff_t>(shared.own_start), t.begin() + static_cast<std::ptrdiff_t>(skip));
            int nesting = 0;
            for (std::size_t i = skip + 1; i < t.size(); ++i) {
                const std::string& x = t[i].text;
                if (x == "(" || x == "[") ++nesting;
                if (x == ")" || x == "]") --nesting;
                if (nesting == 0 && (x == "=" || x == "{" || x == ":" || x == ",")) break;
                own.push_back(t[i]);
            }
            if (has_macro_use(own, 0, own.size())) return type_of(clang_getCursorType(c), where);
            const std::string text = spelled(own, 0, own.size(), std::string::npos);
            return text.empty() ? type_of(clang_getCursorType(c), where) : make(text, where);
        }
        if (has_macro_use(t, 0, end)) return type_of(clang_getCursorType(c), where);
        const std::string text = spelled(t, 0, end, skip);
        return text.empty() ? type_of(clang_getCursorType(c), where) : make(text, where);
    }

    // What a function returns, as written: the declaration before its name, or what follows `->`.
    ir::Type return_type(CXCursor c, const std::vector<Tok>& t, const Shape& shape) const {
        const CXSourceLocation where = clang_getRangeStart(clang_getCursorExtent(c));
        if (!from_macro(c)) {
            if (shape.trailing_return) {
                if (has_macro_use(t, shape.trailing_return->first, shape.trailing_return->second + 1)) return type_of(clang_getCursorResultType(c), where);
                return make(ir::normalize_spelling(written(t, shape.trailing_return->first, shape.trailing_return->second)), where);
            }
            const std::size_t name_at = index_at(t, name_offset(c));
            if (name_at != std::string::npos) {
                // `void (*get())(int)` has its name inside its own type: that is for libclang to spell.
                const bool nested = has_declarator_parentheses(t, name_at) || has_macro_use(t, 0, name_at);
                const std::string text = nested ? std::string() : spelled(t, 0, name_at, std::string::npos);
                if (!text.empty()) return make(text, where);
            }
        }
        return type_of(clang_getCursorResultType(c), where);
    }

    // ---- diagnostics ------------------------------------------------------------------------------

    void report_diagnostics() {
        unsigned reported = 0;
        unsigned more = 0;
        const unsigned count = clang_getNumDiagnostics(tu_);
        for (unsigned i = 0; i < count; ++i) {
            CXDiagnostic d = clang_getDiagnostic(tu_, i);
            if (clang_getDiagnosticSeverity(d) >= CXDiagnostic_Error) {
                if (reported < kMaxParseWarnings) {
                    ++reported;
                    CXFile file = nullptr;
                    unsigned line = 0;
                    unsigned column = 0;
                    clang_getExpansionLocation(clang_getDiagnosticLocation(d), &file, &line, &column, nullptr);
                    ir::SourceLoc where;
                    where.file = file == nullptr ? std::string() : to_string(clang_getFileName(file));
                    if (where.file == main_file_) where.file = file_;
                    where.line = line;
                    where.column = column;
                    diagnostics_.warning(core::code::parse, "libclang: " + to_string(clang_getDiagnosticSpelling(d)) + "; declarations around it may be missing or have wrong types", where);
                } else {
                    ++more;
                }
            }
            clang_disposeDiagnostic(d);
        }
        if (more > 0) diagnostics_.note(core::code::parse, "and " + std::to_string(more) + " more libclang errors");
    }

    // The qualifiers of a function: read from its declaration, or, when a macro wrote it, asked of libclang.
    Shape shape_of(CXCursor c, const std::vector<Tok>& t) const {
        if (!from_macro(c)) return read_shape(t, name_offset(c));
        Shape s;
        s.is_static = clang_CXXMethod_isStatic(c) != 0;
        s.is_virtual = clang_CXXMethod_isVirtual(c) != 0;
        s.is_const = clang_CXXMethod_isConst(c) != 0;
        s.is_pure = clang_CXXMethod_isPureVirtual(c) != 0;
        s.is_defaulted = clang_CXXMethod_isDefaulted(c) != 0;
        const int exception = clang_getCursorExceptionSpecificationType(c);
        s.is_noexcept = exception == CXCursor_ExceptionSpecificationKind_BasicNoexcept || exception == CXCursor_ExceptionSpecificationKind_NoThrow;
        for (CXCursor child : children(c)) {
            if (clang_getCursorKind(child) == CXCursor_CXXOverrideAttr) s.is_override = true;
            if (clang_getCursorKind(child) == CXCursor_CXXFinalAttr) s.is_final = true;
        }
        return s;
    }

    void unsupported(const std::string& message, CXCursor at) {
        diagnostics_.warning(core::code::unsupported, message, location(at));
    }

    // ---- scopes ----------------------------------------------------------------------------------

    void declarations(CXCursor scope) {
        for (CXCursor c : children(scope)) declaration(c);
    }

    void declaration(CXCursor c) {
        if (!in_main_file(c)) return;
        switch (clang_getCursorKind(c)) {
            case CXCursor_Namespace: {
                const std::string name = to_string(clang_getCursorSpelling(c));
                if (name.empty()) return;   // internal linkage: out of reach of a test of a header
                ns_.push_back(name);
                declarations(c);
                ns_.pop_back();
                break;
            }
            case CXCursor_LinkageSpec:
                declarations(c);
                break;
            case CXCursor_InclusionDirective:
                include(c);
                break;
            case CXCursor_ClassDecl:
            case CXCursor_StructDecl:
                if (clang_isCursorDefinition(c) != 0) {
                    if (clang_getSpecializedCursorTemplate(c).kind != CXCursor_InvalidFile) {
                        unsupported("the specialisation '" + specialisation_name(c) + "' is not supported and is skipped", c);
                    } else {
                        class_definition(c, std::nullopt);
                    }
                }
                break;
            case CXCursor_ClassTemplate:
                if (clang_isCursorDefinition(c) != 0) class_definition(c, template_parameters(c));
                break;
            case CXCursor_ClassTemplatePartialSpecialization:
                unsupported("the specialisation '" + specialisation_name(c) + "' is not supported and is skipped", c);
                break;
            case CXCursor_EnumDecl:
                enum_definition(c);
                break;
            case CXCursor_FunctionDecl:
                free_function(c);
                break;
            case CXCursor_FunctionTemplate:
                unsupported("function template '" + to_string(clang_getCursorSpelling(c)) + "' is not supported and is skipped", c);
                break;
            default:
                break;
        }
    }

    // `Box<char, 1>`, `Box<T*>`: the name and the arguments, as they were written.
    std::string specialisation_name(CXCursor c) const {
        const std::string name = to_string(clang_getCursorSpelling(c));
        const std::vector<Tok> t = tokens(c);
        const std::size_t at = index_at(t, name_offset(c));
        if (at == std::string::npos || at + 1 >= t.size() || t[at + 1].text != "<") return name;
        int depth = 0;
        for (std::size_t i = at + 1; i < t.size(); ++i) {
            if (t[i].text == "<") ++depth;
            if (t[i].text == ">") --depth;
            if (t[i].text == ">>") depth -= 2;
            if (depth <= 0) return ir::normalize_spelling(written(t, at, i));
        }
        return name;
    }

    void include(CXCursor c) {
        const Span span = span_of(c);
        const std::string text = slice(span.begin, span.end);
        const std::size_t open = text.find_first_of("<\"", text.find("include"));
        if (open == std::string::npos) return;
        const char closer = text[open] == '<' ? '>' : '"';
        const std::size_t close = text.find(closer, open + 1);
        if (close == std::string::npos) return;
        ir::Include inc;
        inc.is_system = text[open] == '<';
        inc.path = text.substr(open + 1, close - open - 1);
        inc.where = location_at(span.begin);
        model_.includes.push_back(std::move(inc));
    }

    // ---- enums ------------------------------------------------------------------------------------

    void enum_definition(CXCursor c) {
        const std::string name = to_string(clang_getCursorSpelling(c));
        if (is_unnamed(name) || clang_isCursorDefinition(c) == 0) return;   // anonymous, or a forward declaration
        ir::Enum e;
        e.name = name;
        e.ns = ns_;
        e.is_scoped = clang_EnumDecl_isScoped(c) != 0;
        e.underlying = underlying_type(c);
        for (CXCursor item : children(c)) {
            if (clang_getCursorKind(item) == CXCursor_EnumConstantDecl) e.enumerators.push_back(to_string(clang_getCursorSpelling(item)));
        }
        e.header = header_;
        e.where = location(c);
        model_.enums.push_back(std::move(e));
    }

    // `enum class E : std::uint8_t`: the type, if the declaration says one (libclang has one for every enum).
    std::optional<std::string> underlying_type(CXCursor c) const {
        const std::vector<Tok> t = tokens(c);
        const unsigned at = name_offset(c);
        std::size_t i = 0;
        while (i < t.size() && t[i].begin <= at) ++i;
        if (i >= t.size() || t[i].text != ":") return std::nullopt;
        std::size_t last = i + 1;
        while (last < t.size() && t[last].text != "{" && t[last].text != ";") ++last;
        if (last == i + 1) return std::nullopt;
        return ir::normalize_spelling(written(t, i + 1, last - 1));
    }

    // ---- free functions ----------------------------------------------------------------------------

    void free_function(CXCursor c) {
        const std::vector<Tok> t = function_tokens(c);
        ir::Function f;
        f.name = ir::normalize_operator_name(to_string(clang_getCursorSpelling(c)));
        f.ns = ns_;
        const Shape shape = shape_of(c, t);
        f.ret = return_type(c, t, shape);
        f.params = parameters(c);
        f.is_noexcept = shape.is_noexcept;
        f.header = header_;
        f.where = location(c);
        model_.functions.push_back(std::move(f));
    }

    // ---- parameters ----------------------------------------------------------------------------------

    // The parameters of the function itself: not those of a function pointer it returns, which libclang also lists among
    // the children (`void (*get(int a))(int b)` has `b` among them, and before `a`). It can say how many are the function's
    // own for a function but not for a function template, whose children are all there is: those between the
    // parentheses of the parameter list, which the tokens say (the type a conversion function converts to can have
    // parameters of its own, `operator Action<U(Args...)>()`), and its C-style `...` is told by them too.
    std::vector<ir::Param> parameters(CXCursor function) const {
        std::vector<CXCursor> declared;
        const int own = clang_Cursor_getNumArguments(function);
        bool c_variadic = clang_Cursor_isVariadic(function) != 0;
        if (own >= 0) {
            for (int i = 0; i < own; ++i) declared.push_back(clang_Cursor_getArgument(function, static_cast<unsigned>(i)));
        } else {
            ParameterList list;
            if (!from_macro(function)) list = find_parameter_list(function_tokens(function), name_offset(function));
            for (CXCursor p : children(function)) {
                if (clang_getCursorKind(p) != CXCursor_ParmDecl) continue;
                const unsigned at = span_of(p).begin;
                if (list.found && (at < list.open || at > list.close)) continue;
                declared.push_back(p);
            }
            c_variadic = c_variadic || list.c_variadic;
        }
        std::vector<ir::Param> params;
        for (CXCursor p : declared) {
            ir::Param param;
            param.type = declared_type(p);
            param.name = to_string(clang_getCursorSpelling(p));
            param.default_value = default_value(p);
            params.push_back(std::move(param));
        }
        if (c_variadic) {
            ir::Param ellipsis;
            ellipsis.type.spelling = "...";
            ellipsis.type.base = "...";
            ellipsis.type.origin = ir::Origin::Declared;
            ellipsis.type.where = location(function);
            params.push_back(std::move(ellipsis));
        }
        return params;
    }

    // The text of the default argument of a parameter that a macro declares, from the spellings of the tokens
    // libclang tokenises for it. The offsets of a token that came out of a macro are those of the place where the
    // macro was used, so a slice of the text would be a piece of whatever is there; and the range of the tokens
    // runs on past the end of the expression, to the end of the use, so it is cut where the expression ends: at a `,`
    // outside parentheses and angle brackets, at a `)` that closes nothing, or at a `;`.
    std::string default_by_tokens(CXCursor expression) const {
        CXToken* raw = nullptr;
        unsigned count = 0;
        clang_tokenize(tu_, clang_getCursorExtent(expression), &raw, &count);
        std::string out;
        const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
        int depth = 0;
        int angles = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (clang_getTokenKind(raw[i]) == CXToken_Comment) continue;
            const std::string text = to_string(clang_getTokenSpelling(tu_, raw[i]));
            if (text == "(" || text == "[" || text == "{") ++depth;
            if (text == ")" || text == "]" || text == "}") {
                if (depth == 0) break;
                --depth;
            }
            if (depth == 0 && text == "<") ++angles;
            if (depth == 0 && text == ">" && angles > 0) --angles;
            if (depth == 0 && text == ">>" && angles > 0) angles = std::max(0, angles - 2);
            if (depth == 0 && angles == 0 && (text == "," || text == ";")) break;
            if (!out.empty() && !text.empty() && word(out.back()) && word(text.front())) out += ' ';
            out += text;
        }
        clang_disposeTokens(tu_, raw, count);
        return out;
    }

    // What follows the first top-level '=' of a parameter, as it was written.
    std::optional<std::string> default_value(CXCursor param) const {
        if (from_macro(param)) {
            for (CXCursor child : children(param)) {
                if (clang_isExpression(clang_getCursorKind(child)) == 0) continue;
                const std::string text = default_by_tokens(child);
                return text.empty() ? std::nullopt : std::optional<std::string>(text);
            }
            return std::nullopt;
        }
        const std::vector<Tok> t = tokens(param);
        int depth = 0;
        for (std::size_t i = 0; i < t.size(); ++i) {
            const std::string& x = t[i].text;
            if (x == "(" || x == "[" || x == "{") ++depth;
            if (x == ")" || x == "]" || x == "}") --depth;
            if (depth == 0 && x == "=" && i + 1 < t.size()) return written(t, i + 1, t.size() - 1);
        }
        return std::nullopt;
    }

    // ---- classes ------------------------------------------------------------------------------------

    ir::Class& find_or_add_class(const std::string& name) {
        for (ir::Class& c : model_.classes) {
            if (c.name == name && c.ns == ns_) return c;
        }
        model_.classes.emplace_back();
        model_.classes.back().name = name;
        model_.classes.back().ns = ns_;
        return model_.classes.back();
    }

    // "typename T, int N": the text between the angle brackets of the template header.
    std::string template_parameters(CXCursor c) const {
        bool any = false;
        Span all;
        for (CXCursor p : children(c)) {
            const CXCursorKind k = clang_getCursorKind(p);
            if (k != CXCursor_TemplateTypeParameter && k != CXCursor_NonTypeTemplateParameter && k != CXCursor_TemplateTemplateParameter) continue;
            const Span s = span_of(p);
            if (!any) all.begin = s.begin;
            all.end = s.end;
            any = true;
        }
        return any ? ir::collapse_spaces(slice(all.begin, all.end)) : std::string();
    }

    // The name after the `class` or `struct` of a definition, as it was written (`Outer::Inner`, `Tpl<T>::Node`).
    std::string written_class_name(CXCursor c) const {
        const std::vector<Tok> t = tokens(c);
        std::size_t first = 0;
        while (first < t.size() && t[first].text != "class" && t[first].text != "struct" && t[first].text != "union") ++first;
        ++first;
        std::size_t last = first;
        int angles = 0;
        for (; last < t.size(); ++last) {
            const std::string& x = t[last].text;
            if (x == "<") ++angles;
            if (x == ">") --angles;
            if (x == ">>") angles -= 2;
            if (angles <= 0 && (x == "{" || x == ":" || x == "final")) break;
        }
        return first < last ? ir::normalize_spelling(written(t, first, last - 1)) : to_string(clang_getCursorSpelling(c));
    }

    void class_definition(CXCursor c, const std::optional<std::string>& template_params) {
        const std::string name = to_string(clang_getCursorSpelling(c));
        if (is_unnamed(name)) return;
        if (clang_equalCursors(clang_getCursorSemanticParent(c), clang_getCursorLexicalParent(c)) == 0) {
            // `struct Outer::Inner { ... };`: defined outside the class that declares it.
            unsupported("the class '" + written_class_name(c) + "' is defined outside the class that declares it, which is not supported; it is skipped", c);
            return;
        }
        read_class(find_or_add_class(name), c, template_params);
    }

    bool is_struct_cursor(CXCursor c) const {
        const CXCursorKind k = clang_getCursorKind(c);
        return (k == CXCursor_ClassTemplate ? clang_getTemplateCursorKind(c) : k) == CXCursor_StructDecl;
    }

    void read_class(ir::Class& cls, CXCursor c, const std::optional<std::string>& template_params) {
        if (!cls.where.known()) {
            cls.is_struct = is_struct_cursor(c);
            cls.header = header_;
            cls.origin = ir::Origin::Declared;
            cls.where = location(c);
            cls.template_params = template_params;
            read_bases(c, cls);
        }
        ClassState state{cls, ir::QtRole::None, span_of(c).begin};
        members(c, state);
        read_qt_class_markers(c, cls);
    }

    // The base clause as written: `public Base<int>, virtual Other`. Read from the text because
    // libclang drops a base it cannot resolve, and the clause is what the user wrote either way.
    void read_bases(CXCursor c, ir::Class& cls) const {
        const std::vector<Tok> t = tokens(c);
        const unsigned at = name_offset(c);
        std::size_t i = 0;
        while (i < t.size() && t[i].begin <= at) ++i;
        if (i < t.size() && t[i].text == "final") ++i;
        if (i >= t.size() || t[i].text != ":") return;
        ++i;

        ir::Access access = cls.is_struct ? ir::Access::Public : ir::Access::Private;
        bool is_virtual = false;
        std::size_t first = std::string::npos;
        std::size_t last = 0;
        int depth = 0;
        const auto flush = [&] {
            if (first != std::string::npos) {
                ir::BaseSpec base;
                base.name = ir::normalize_spelling(written(t, first, last));
                base.access = access;
                base.is_virtual = is_virtual;
                cls.bases.push_back(std::move(base));
            }
            access = cls.is_struct ? ir::Access::Public : ir::Access::Private;
            is_virtual = false;
            first = std::string::npos;
        };
        for (; i < t.size() && !(depth == 0 && t[i].text == "{"); ++i) {
            const std::string& x = t[i].text;
            if (depth == 0 && x == ",") {
                flush();
                continue;
            }
            if (first == std::string::npos && (x == "public" || x == "protected" || x == "private")) {
                access = x == "public" ? ir::Access::Public : x == "protected" ? ir::Access::Protected : ir::Access::Private;
                continue;
            }
            if (first == std::string::npos && x == "virtual") {
                is_virtual = true;
                continue;
            }
            if (x == "<" || x == "(") ++depth;
            if (x == ">" || x == ")") --depth;
            if (x == ">>") depth -= 2;
            if (first == std::string::npos) first = i;
            last = i;
        }
        flush();
    }

    // ---- Qt ----------------------------------------------------------------------------------------

    bool marker_between(QtMarker::Kind kind, unsigned from, unsigned to) const {
        for (const QtMarker& m : markers_) {
            if (m.kind == kind && m.offset >= from && m.offset < to) return true;
        }
        return false;
    }

    ir::QtRole section_role(unsigned access_specifier_offset) const {
        for (const QtMarker& m : markers_) {
            if (m.offset != access_specifier_offset) continue;
            if (m.kind == QtMarker::Kind::SlotsSection) return ir::QtRole::Slot;
            if (m.kind == QtMarker::Kind::SignalsSection) return ir::QtRole::Signal;
        }
        return ir::QtRole::None;
    }

    // The class-level macros that sit directly in this class, not in one nested inside it.
    void read_qt_class_markers(CXCursor c, ir::Class& cls) const {
        const Span whole = span_of(c);
        std::vector<Span> nested;
        for (CXCursor child : children(c)) {
            const CXCursorKind k = clang_getCursorKind(child);
            if (is_class_kind(k) || k == CXCursor_UnionDecl) nested.push_back(span_of(child));
        }
        const auto direct = [&](unsigned offset) {
            return std::none_of(nested.begin(), nested.end(), [offset](const Span& r) { return offset >= r.begin && offset < r.end; });
        };
        for (const QtMarker& m : markers_) {
            if (m.offset < whole.begin || m.offset >= whole.end || !direct(m.offset)) continue;
            if (m.kind == QtMarker::Kind::QObject) cls.is_qobject = true;
            if (m.kind == QtMarker::Kind::Gadget) cls.is_gadget = true;
            if (m.kind == QtMarker::Kind::DisableCopy) cls.disable_copy = true;
        }
    }

    // ---- members -------------------------------------------------------------------------------------

    static void add_nested(ir::Class& cls, const std::string& name) {
        if (!is_unnamed(name) && std::find(cls.nested_types.begin(), cls.nested_types.end(), name) == cls.nested_types.end()) {
            cls.nested_types.push_back(name);
        }
    }

    void members(CXCursor body, ClassState& state) {
        for (CXCursor c : children(body)) {
            const CXCursorKind kind = clang_getCursorKind(c);
            const ir::Access access = to_access(clang_getCXXAccessSpecifier(c), ir::Access::Public);
            if (kind == CXCursor_CXXAccessSpecifier) {
                state.section = section_role(span_of(c).begin);
            } else if (kind == CXCursor_FieldDecl) {
                field(c, state, false);
            } else if (kind == CXCursor_VarDecl) {
                field(c, state, true);   // a static data member
            } else if (is_method_kind(kind)) {
                method(c, state, false);
            } else if (kind == CXCursor_FunctionTemplate) {
                method(c, state, true);
            } else if (is_class_kind(kind) || kind == CXCursor_UnionDecl) {
                const std::string name = to_string(clang_getCursorSpelling(c));
                add_nested(state.cls, name);
                if (kind != CXCursor_UnionDecl) nested_class(c, state, access);
            } else if (kind == CXCursor_EnumDecl) {
                const std::string name = to_string(clang_getCursorSpelling(c));
                add_nested(state.cls, name);
                if (access == ir::Access::Public && !is_unnamed(name) &&
                    std::find(state.cls.nested_enums.begin(), state.cls.nested_enums.end(), name) == state.cls.nested_enums.end()) {
                    state.cls.nested_enums.push_back(name);
                }
            } else if (kind == CXCursor_TypedefDecl || kind == CXCursor_TypeAliasDecl) {
                add_nested(state.cls, to_string(clang_getCursorSpelling(c)));
            } else if (kind == CXCursor_UsingDeclaration) {
                inherited_constructors(c);
            }
            state.previous_end = span_of(c).end;
        }
    }

    void nested_class(CXCursor c, ClassState& outer, ir::Access access) {
        const std::string name = to_string(clang_getCursorSpelling(c));
        if (is_unnamed(name) || clang_isCursorDefinition(c) == 0) return;
        ir::Class* nested = nullptr;
        for (ir::Class& n : outer.cls.nested_classes) {
            if (n.name == name) nested = &n;
        }
        if (nested == nullptr) {
            outer.cls.nested_classes.emplace_back();
            nested = &outer.cls.nested_classes.back();
            nested->name = name;
            nested->ns = outer.cls.ns;
            nested->nested_access = access;
        }
        read_class(*nested, c, std::nullopt);
    }

    // `using Base::Base;` makes Base's constructors the derived class's own.
    void inherited_constructors(CXCursor c) {
        const std::vector<Tok> t = tokens(c);
        // using, [typename], a::b::Name, ::, Name, ;
        std::vector<std::string> words;
        for (const Tok& x : t) {
            if (x.text != ";") words.push_back(x.text);
        }
        if (words.size() < 4 || words.front() != "using") return;
        const std::string& tail = words[words.size() - 1];
        const std::string& scope = words[words.size() - 3];
        if (words[words.size() - 2] == "::" && tail == scope) {
            unsupported("'using " + compact(t, 1, t.size() - (t.back().text == ";" ? 1 : 0)) + ";' inherits constructors, which are not supported", c);
        }
    }

    ir::QtRole role_for(const ClassState& state, bool invokable) const {
        if (state.section != ir::QtRole::None) return state.section;
        return invokable ? ir::QtRole::Invokable : ir::QtRole::None;
    }

    // `operator Matcher<T>`: the type after `operator`, as written. libclang names the conversion function of a template
    // with the canonical names of its parameters (`operator Matcher<type-parameter-0-0>`).
    std::string conversion_name(CXCursor c, const std::vector<Tok>& t, const std::string& api_name) const {
        if (!from_macro(c)) {
            const std::size_t at = index_at(t, name_offset(c));
            if (at != std::string::npos && at < t.size() && t[at].text == "operator") {
                std::size_t end = at + 1;
                int angles = 0;
                for (; end < t.size(); ++end) {
                    const std::string& x = t[end].text;
                    if (x == "<") ++angles;
                    if (x == ">") --angles;
                    if (x == ">>") angles -= 2;
                    if (x == "(" && angles <= 0) break;
                }
                const std::string written = end > at + 1 && end < t.size() ? spelled(t, at + 1, end, std::string::npos) : std::string();
                if (!written.empty()) return "operator " + written;
            }
        }
        return "operator " + ir::normalize_spelling(api_name.substr(std::min<std::size_t>(api_name.size(), 8)));
    }

    void method(CXCursor c, ClassState& state, bool is_template) {
        const CXCursorKind kind = is_template ? clang_getTemplateCursorKind(c) : clang_getCursorKind(c);
        if (is_template && !is_method_kind(kind)) return;   // a member class template, for one: not a member to test
        const Span span = span_of(c);
        const std::vector<Tok> t = function_tokens(c);
        const Shape shape = shape_of(c, t);

        ir::Method m;
        const std::string spelled = to_string(clang_getCursorSpelling(c));
        switch (kind) {
            case CXCursor_Constructor:   // not `Box<T, N>`, which is what libclang calls it in a class template
                m.kind = ir::MethodKind::Constructor;
                m.name = state.cls.name;
                break;
            case CXCursor_Destructor:
                m.kind = ir::MethodKind::Destructor;
                m.name = "~" + state.cls.name;
                break;
            case CXCursor_ConversionFunction:
                m.kind = ir::MethodKind::Conversion;
                m.name = conversion_name(c, t, spelled);
                break;
            default:
                m.name = ir::normalize_operator_name(spelled);
                m.kind = ir::is_operator_name(m.name) ? ir::MethodKind::Operator : ir::MethodKind::Normal;
                break;
        }
        if (m.kind == ir::MethodKind::Normal || m.kind == ir::MethodKind::Operator) {
            m.ret = return_type(c, t, shape);
        }
        m.params = parameters(c);
        m.access = to_access(clang_getCXXAccessSpecifier(c), ir::Access::Public);
        const bool invokable = marker_between(QtMarker::Kind::Invokable, state.previous_end, span.begin);
        m.qt_role = role_for(state, invokable);
        m.is_template = is_template;
        m.is_static = shape.is_static;
        m.is_virtual = shape.is_virtual;
        m.is_explicit = shape.is_explicit;
        m.is_const = shape.is_const;
        m.is_noexcept = shape.is_noexcept;
        m.is_override = shape.is_override;
        m.is_final = shape.is_final;
        m.ref_qualifier = shape.ref;
        m.is_pure = shape.is_pure;
        m.is_deleted = shape.is_deleted;
        m.is_defaulted = shape.is_defaulted;
        m.origin = ir::Origin::Declared;
        m.where = location(c);
        state.cls.methods.push_back(std::move(m));
    }

    void field(CXCursor c, ClassState& state, bool is_static) {
        const std::string name = to_string(clang_getCursorSpelling(c));
        for (const ir::Field& existing : state.cls.fields) {
            if (existing.name == name) return;
        }
        ir::Field f;
        f.type = declared_type(c);
        f.name = name;
        f.access = to_access(clang_getCXXAccessSpecifier(c), ir::Access::Public);
        f.is_static = is_static;
        f.origin = ir::Origin::Declared;
        f.where = location(c);
        state.cls.fields.push_back(std::move(f));
    }

    CXTranslationUnit            tu_;
    const std::string&           text_;
    const std::vector<QtMarker>& markers_;
    const std::string&           main_file_;
    const std::string&           file_;
    const std::string&           header_;
    ir::Model&                   model_;
    core::Diagnostics&           diagnostics_;
    std::vector<std::string>     ns_;
    std::vector<unsigned>        macro_expansions_;   // where, in the text, a macro is used
};

}  // namespace

ir::Model read_header_clang(const core::SourceFile& source, const std::string& include_path, const syntax::MacroTable& macros,
                            const ClangOptions& options, core::Diagnostics& diagnostics) {
    ir::Model model;
    const syntax::Preprocessed pre = syntax::preprocess(source.text, macros);
    const std::string opened = options.absolute_path.empty() ? source.path : options.absolute_path;

    std::vector<std::string> arguments = {"-x", "c++"};
    arguments.insert(arguments.end(), options.arguments.begin(), options.arguments.end());
    std::vector<const char*> argv;
    for (const std::string& a : arguments) argv.push_back(a.c_str());

    CXUnsavedFile unsaved;
    unsaved.Filename = opened.c_str();
    unsaved.Contents = pre.text.c_str();
    unsaved.Length = static_cast<unsigned long>(pre.text.size());

    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit tu = nullptr;
    const unsigned flags = CXTranslationUnit_DetailedPreprocessingRecord | CXTranslationUnit_SkipFunctionBodies;
    const CXErrorCode error = clang_parseTranslationUnit2(index, opened.c_str(), argv.data(), static_cast<int>(argv.size()), &unsaved, 1, flags, &tu);
    if (error != CXError_Success || tu == nullptr) {
        ir::SourceLoc where;
        where.file = source.path;
        diagnostics.error(core::code::parse, "libclang could not parse this file (error " + std::to_string(static_cast<int>(error)) + ")", where);
        if (tu != nullptr) clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        return model;
    }

    Reader(tu, pre.text, pre.markers, opened, source.path, include_path, model, diagnostics).run();
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    return model;
}

#else  // !MT_WITH_LIBCLANG

ir::Model read_header_clang(const core::SourceFile& source, const std::string&, const syntax::MacroTable&, const ClangOptions&,
                            core::Diagnostics& diagnostics) {
    ir::SourceLoc where;
    where.file = source.path;
    diagnostics.error(core::code::parse,
                      "this build of mirrortest has no libclang frontend; use --frontend tree-sitter, or build with -DMT_WITH_LIBCLANG=ON", where);
    return {};
}

#endif

}  // namespace mt::frontend
