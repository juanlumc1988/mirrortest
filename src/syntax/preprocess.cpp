// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/syntax/preprocess.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace mt::syntax {

namespace {

constexpr std::size_t npos = std::string_view::npos;

// ASCII only, on purpose: <cctype> depends on the locale, and bytes >= 0x80
// are UTF-8 identifier characters that must never split a name.
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_high(char c) { return static_cast<unsigned char>(c) >= 0x80; }
bool is_ident_start(char c) { return is_alpha(c) || c == '_' || is_high(c); }
bool is_ident_char(char c) { return is_ident_start(c) || is_digit(c); }
bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}
bool is_upper_or_digit_or_underscore(char c) {
    return (c >= 'A' && c <= 'Z') || is_digit(c) || c == '_';
}

// ^[A-Z][A-Z0-9_]*$ -- the shape of an export macro.
bool is_all_caps(std::string_view word) {
    if (word.empty() || !(word[0] >= 'A' && word[0] <= 'Z')) {
        return false;
    }
    return std::all_of(word.begin(), word.end(), is_upper_or_digit_or_underscore);
}

bool is_access_specifier(std::string_view word) {
    return word == "public" || word == "protected" || word == "private";
}

template <std::size_t N>
bool one_of(const char* const (&names)[N], std::string_view word) {
    return std::find(names, names + N, word) != names + N;
}

// Replaced by spaces, optionally together with a balanced "(...)".
const char* const kBlankFunctions[] = {
    "Q_PROPERTY", "Q_ENUM", "Q_ENUMS", "Q_FLAG", "Q_FLAGS", "Q_CLASSINFO",
    "Q_INTERFACES", "Q_DECLARE_METATYPE", "Q_DECLARE_PRIVATE", "Q_DECLARE_PUBLIC",
    "Q_PRIVATE_SLOT", "Q_DECL_DEPRECATED_X", "Q_DISABLE_COPY", "Q_DISABLE_COPY_MOVE",
};

// Replaced by spaces.
const char* const kBlankObjects[] = {
    "Q_NAMESPACE", "Q_REQUIRED_RESULT", "Q_DECL_DEPRECATED",
    // `QBENCHMARK { ... }` is a loop the macro writes; without it the braces are a block.
    "QBENCHMARK", "QBENCHMARK_ONCE",
};

// Replaced by the keyword they stand for. Every keyword is shorter than its
// macro, which is what lets the replacement keep the text the same length.
struct Replacement {
    const char* macro;
    const char* keyword;
};
const Replacement kReplacements[] = {
    {"Q_DECL_OVERRIDE", "override"},
    {"Q_DECL_FINAL", "final"},
    {"Q_DECL_NOEXCEPT", "noexcept"},
    {"Q_DECL_CONSTEXPR", "constexpr"},
    {"Q_NULLPTR", "nullptr"},
};

// Stands in for the previous token when it was a literal.
constexpr std::string_view kLiteral = "<literal>";

struct Token {
    std::string_view text;
    std::size_t      start = npos;
};

class Preprocessor {
public:
    Preprocessor(std::string_view src, const MacroTable& table) : src_(src), table_(table) {
        result_.text.assign(src.data(), src.size());
    }

    Preprocessed run() {
        const std::size_t n = src_.size();
        if (n > std::numeric_limits<unsigned>::max()) {
            return std::move(result_);
        }

        std::size_t i = 0;
        if (src_.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            blank(0, 3);
            i = 3;
        }

        while (i < n) {
            const char c = src_[i];
            const char next = i + 1 < n ? src_[i + 1] : '\0';

            if (is_space(c)) {
                ++i;
            } else if (c == '/' && next == '/') {
                i = skip_line_comment(i);
            } else if (c == '/' && next == '*') {
                i = skip_block_comment(i);
            } else if (c == '#' && at_line_start(i)) {
                i = skip_directive(i);
            } else if (c == '"') {
                i = skip_quoted(i, '"');
                note_literal();
            } else if (c == '\'') {
                i = skip_quoted(i, '\'');
                note_literal();
            } else if (is_digit(c) || (c == '.' && is_digit(next))) {
                i = skip_number(i);
                note_literal();
            } else if (is_ident_start(c)) {
                i = identifier(i);
            } else {
                prev_ = {src_.substr(i, 1), i};
                ++i;
            }
        }
        return std::move(result_);
    }

private:
    // ---- recording ---------------------------------------------------------

    void note_literal() { prev_ = {kLiteral, npos}; }

    void marker(QtMarker::Kind kind, std::size_t offset) {
        result_.markers.push_back({kind, static_cast<unsigned>(offset)});
    }

    // Spaces over [begin, end), keeping line breaks so positions stay put.
    void blank(std::size_t begin, std::size_t end) {
        for (std::size_t k = begin; k < end; ++k) {
            if (result_.text[k] != '\n' && result_.text[k] != '\r') {
                result_.text[k] = ' ';
            }
        }
        result_.blanked.push_back({static_cast<unsigned>(begin), static_cast<unsigned>(end)});
    }

    // `keyword` followed by spaces over [begin, end). keyword.size() <= end - begin.
    void replace(std::size_t begin, std::size_t end, std::string_view keyword) {
        blank(begin, end);
        std::copy(keyword.begin(), keyword.end(), result_.text.begin() + static_cast<long>(begin));
    }

    // ---- skipping text that merely contains macro names ---------------------

    // `i` is at the first '/'. A line comment continues over a trailing '\'.
    std::size_t skip_line_comment(std::size_t i) const {
        const std::size_t n = src_.size();
        std::size_t j = i + 2;
        while (j < n) {
            const std::size_t eol = src_.find('\n', j);
            if (eol == npos) {
                return n;
            }
            std::size_t last = eol;
            if (last > i && src_[last - 1] == '\r') {
                --last;
            }
            if (last > i + 1 && src_[last - 1] == '\\') {
                j = eol + 1;   // continued: the next line is still the comment
                continue;
            }
            return eol;
        }
        return n;
    }

    std::size_t skip_block_comment(std::size_t i) const {
        const std::size_t end = src_.find("*/", i + 2);
        return end == npos ? src_.size() : end + 2;
    }

    // `i` is at the opening quote. An unterminated literal ends at the line
    // break: C++ forbids a raw newline inside one, and swallowing the rest of
    // the file would hide every macro after it.
    std::size_t skip_quoted(std::size_t i, char quote) const {
        const std::size_t n = src_.size();
        std::size_t j = i + 1;
        while (j < n) {
            const char c = src_[j];
            if (c == '\\') {
                if (j + 2 < n && src_[j + 1] == '\r' && src_[j + 2] == '\n') {
                    j += 3;
                } else {
                    j += 2;
                }
            } else if (c == quote) {
                return j + 1;
            } else if (c == '\n') {
                return j;
            } else {
                ++j;
            }
        }
        return n;
    }

    // `i` is at the '"' of R"delim( ... )delim".
    std::size_t skip_raw_string(std::size_t i) const {
        const std::size_t n = src_.size();
        std::size_t j = i + 1;
        while (j < n && j - (i + 1) <= 16 && src_[j] != '(' && src_[j] != ')' &&
               src_[j] != '"' && src_[j] != '\\' && !is_space(src_[j])) {
            ++j;
        }
        if (j >= n || src_[j] != '(') {
            return skip_quoted(i, '"');   // not a valid raw string after all
        }
        const std::string terminator = ")" + std::string(src_.substr(i + 1, j - (i + 1))) + "\"";
        const std::size_t end = src_.find(terminator, j + 1);
        return end == npos ? n : end + terminator.size();
    }

    // A preprocessing number: digits, letters, '.', exponent signs and digit
    // separators. The separator is why this exists -- 1'000'000 must not be
    // read as the start of a character literal.
    std::size_t skip_number(std::size_t i) const {
        const std::size_t n = src_.size();
        std::size_t j = i;
        while (j < n) {
            const char c = src_[j];
            if ((c == 'e' || c == 'E' || c == 'p' || c == 'P') && j + 1 < n &&
                (src_[j + 1] == '+' || src_[j + 1] == '-')) {
                j += 2;
            } else if (is_ident_char(c) || c == '.') {
                ++j;
            } else if (c == '\'' && j + 1 < n && is_ident_char(src_[j + 1])) {
                j += 2;
            } else {
                break;
            }
        }
        return j;
    }

    bool at_line_start(std::size_t i) const {
        // Looks at the output text, where a byte-order mark is already spaces.
        std::size_t k = i;
        while (k > 0 && (result_.text[k - 1] == ' ' || result_.text[k - 1] == '\t')) {
            --k;
        }
        return k == 0 || result_.text[k - 1] == '\n';
    }

    // A directive, to the end of its logical line.
    std::size_t skip_directive(std::size_t i) const {
        const std::size_t n = src_.size();
        std::size_t j = i + 1;
        while (j < n) {
            const char c = src_[j];
            const char next = j + 1 < n ? src_[j + 1] : '\0';
            if (c == '\n') {
                std::size_t last = j;
                if (last > i && src_[last - 1] == '\r') {
                    --last;
                }
                if (last > i && src_[last - 1] == '\\') {
                    ++j;   // continued
                    continue;
                }
                return j;
            }
            if (c == '/' && next == '*') {
                j = skip_block_comment(j);
            } else if (c == '/' && next == '/') {
                return skip_line_comment(j);
            } else if (c == '"') {
                j = skip_quoted(j, '"');
            } else {
                ++j;
            }
        }
        return n;
    }

    // First significant character at or after `i`, skipping whitespace and
    // comments; src_.size() if there is none.
    std::size_t next_significant(std::size_t i) const {
        const std::size_t n = src_.size();
        while (i < n) {
            if (is_space(src_[i])) {
                ++i;
            } else if (src_[i] == '/' && i + 1 < n && src_[i + 1] == '/') {
                i = skip_line_comment(i);
            } else if (src_[i] == '/' && i + 1 < n && src_[i + 1] == '*') {
                i = skip_block_comment(i);
            } else {
                break;
            }
        }
        return i;
    }

    // Spaces and tabs only. Deliberately not newlines: after `MY_ATTR` a '('
    // on the next line is far more likely a statement than its arguments.
    std::size_t skip_hspace(std::size_t i) const {
        while (i < src_.size() && (src_[i] == ' ' || src_[i] == '\t')) {
            ++i;
        }
        return i;
    }

    // `open` is at a '('. Index of the matching ')', or npos.
    std::size_t matching_paren(std::size_t open) const {
        const std::size_t n = src_.size();
        int depth = 0;
        std::size_t j = open;
        while (j < n) {
            const char c = src_[j];
            if (c == '(') {
                ++depth;
                ++j;
            } else if (c == ')') {
                if (--depth == 0) {
                    return j;
                }
                ++j;
            } else if (c == '"') {
                j = (j > open && src_[j - 1] == 'R') ? skip_raw_string(j) : skip_quoted(j, '"');
            } else if (c == '\'') {
                j = skip_quoted(j, '\'');
            } else if (c == '/' && j + 1 < n && src_[j + 1] == '/') {
                j = skip_line_comment(j);
            } else if (c == '/' && j + 1 < n && src_[j + 1] == '*') {
                j = skip_block_comment(j);
            } else {
                ++j;
            }
        }
        return npos;
    }

    // ---- identifiers -------------------------------------------------------

    bool is_ignored(std::string_view word) const {
        return std::find(table_.ignore.begin(), table_.ignore.end(), word) != table_.ignore.end();
    }

    // The name at [i, j) is a macro that may carry a "(...)" argument list.
    std::size_t function_like(std::size_t i, std::size_t j, std::string_view word) {
        const std::size_t k = skip_hspace(j);
        std::size_t end = j;
        if (k < src_.size() && src_[k] == '(') {
            const std::size_t close = matching_paren(k);
            if (close != npos) {
                end = close + 1;
            }
        }
        if (word == "Q_DISABLE_COPY" || word == "Q_DISABLE_COPY_MOVE") {
            marker(QtMarker::Kind::DisableCopy, i);
        }
        blank(i, end);
        return end;
    }

    // `slots:` / `signals:` and their Q_ spellings. Returns true if handled.
    //
    // Both are ordinary identifiers outside a class body, so they are only
    // touched when they look like a section label: followed by a single ':'
    // and preceded by an access specifier or a member boundary. That keeps
    // `int slots = 0;` and `cond ? slots : other` intact.
    bool section_label(std::size_t i, std::size_t j, std::string_view word) {
        const std::size_t n = src_.size();
        const std::size_t k = skip_hspace(j);
        if (k >= n || src_[k] != ':' || (k + 1 < n && src_[k + 1] == ':')) {
            return false;
        }

        const bool after_access = is_access_specifier(prev_.text);
        if (word == "slots" || word == "Q_SLOTS") {
            if (!after_access) {
                return false;
            }
            marker(QtMarker::Kind::SlotsSection, prev_.start);
            blank(i, j);
            return true;
        }

        const bool at_boundary = prev_.text == "{" || prev_.text == "}" || prev_.text == ";" ||
                                 prev_.text == ":";
        if (after_access) {
            marker(QtMarker::Kind::SignalsSection, prev_.start);
            blank(i, j);
        } else if (at_boundary) {
            marker(QtMarker::Kind::SignalsSection, i);
            replace(i, j, "public");
            prev_ = {"public", i};
        } else {
            return false;
        }
        return true;
    }

    // `class FOO_EXPORT Widget`: an all-caps name sitting between the class
    // key and the real name. `class FOO final` has no second name, so FOO is
    // the class there and stays.
    bool export_macro(std::size_t j, std::string_view word) const {
        if ((prev_.text != "class" && prev_.text != "struct") || !is_all_caps(word)) {
            return false;
        }
        const std::size_t k = next_significant(j);
        if (k >= src_.size() || !is_ident_start(src_[k])) {
            return false;
        }
        std::size_t e = k;
        while (e < src_.size() && is_ident_char(src_[e])) {
            ++e;
        }
        return src_.substr(k, e - k) != "final";
    }

    // `i` is at the first character of an identifier. Returns the index after
    // whatever was consumed.
    std::size_t identifier(std::size_t i) {
        const std::size_t n = src_.size();
        std::size_t j = i;
        while (j < n && is_ident_char(src_[j])) {
            ++j;
        }
        const std::string_view word = src_.substr(i, j - i);

        // u8"..", L'..', R"(..)": a prefix glued to a literal is part of it.
        if (j < n && (src_[j] == '"' || src_[j] == '\'')) {
            const bool raw = word == "R" || word == "u8R" || word == "uR" || word == "UR" ||
                             word == "LR";
            const bool encoded = word == "u8" || word == "u" || word == "U" || word == "L";
            if (src_[j] == '"' && raw) {
                note_literal();
                return skip_raw_string(j);
            }
            if (encoded) {
                note_literal();
                return skip_quoted(j, src_[j]);
            }
        }

        if (word == "Q_OBJECT") {
            marker(QtMarker::Kind::QObject, i);
            blank(i, j);
            return j;
        }
        if (word == "Q_GADGET") {
            marker(QtMarker::Kind::Gadget, i);
            blank(i, j);
            return j;
        }
        if (word == "Q_INVOKABLE") {
            marker(QtMarker::Kind::Invokable, i);
            blank(i, j);
            return j;
        }
        if (one_of(kBlankObjects, word)) {
            blank(i, j);
            return j;
        }
        for (const Replacement& r : kReplacements) {
            if (word == r.macro) {
                replace(i, j, r.keyword);
                return j;
            }
        }
        if (one_of(kBlankFunctions, word) || is_ignored(word)) {
            return function_like(i, j, word);
        }
        if ((word == "slots" || word == "Q_SLOTS" || word == "signals" || word == "Q_SIGNALS") &&
            section_label(i, j, word)) {
            return j;
        }
        if (export_macro(j, word)) {
            blank(i, j);
            return j;
        }

        prev_ = {word, i};
        return j;
    }

    std::string_view   src_;
    const MacroTable&  table_;
    Preprocessed       result_;
    Token              prev_;
};

}  // namespace

Preprocessed preprocess(std::string_view source, const MacroTable& table) {
    return Preprocessor(source, table).run();
}

}  // namespace mt::syntax
