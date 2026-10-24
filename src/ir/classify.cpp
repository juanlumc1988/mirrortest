// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/ir/classify.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <set>

#include "mirrortest/ir/type.h"

namespace mt::ir {

namespace {

bool starts_with(std::string_view s, std::string_view prefix) { return s.compare(0, prefix.size(), prefix) == 0; }
bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool is_ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

struct Entry {
    std::string_view name;
    std::string_view header;
};

// Sorted by name. `std::` is not part of the key.
constexpr Entry kStdTypes[] = {
    {"array", "array"},
    {"atomic", "atomic"},
    {"basic_string", "string"},
    {"bitset", "bitset"},
    {"byte", "cstddef"},
    {"deque", "deque"},
    {"exception", "exception"},
    {"function", "functional"},
    {"initializer_list", "initializer_list"},
    {"invalid_argument", "stdexcept"},
    {"istream", "iosfwd"},
    {"list", "list"},
    {"logic_error", "stdexcept"},
    {"map", "map"},
    {"max_align_t", "cstddef"},
    {"multimap", "map"},
    {"multiset", "set"},
    {"mutex", "mutex"},
    {"nullptr_t", "cstddef"},
    {"optional", "optional"},
    {"ostream", "iosfwd"},
    {"out_of_range", "stdexcept"},
    {"pair", "utility"},
    {"queue", "queue"},
    {"runtime_error", "stdexcept"},
    {"set", "set"},
    {"shared_ptr", "memory"},
    {"stack", "stack"},
    {"string", "string"},
    {"string_view", "string_view"},
    {"thread", "thread"},
    {"tuple", "tuple"},
    {"u16string", "string"},
    {"u32string", "string"},
    {"unique_ptr", "memory"},
    {"unordered_map", "unordered_map"},
    {"unordered_multimap", "unordered_map"},
    {"unordered_multiset", "unordered_set"},
    {"unordered_set", "unordered_set"},
    {"variant", "variant"},
    {"vector", "vector"},
    {"weak_ptr", "memory"},
    {"wstring", "string"},
};

const Entry* find_entry(std::string_view name) {
    const auto* end = std::end(kStdTypes);
    const auto* found = std::lower_bound(std::begin(kStdTypes), end, name, [](const Entry& e, std::string_view n) { return e.name < n; });
    return found != end && found->name == name ? found : nullptr;
}

// uint8_t, int_fast16_t, intptr_t, uintmax_t ...
bool is_fixed_width_int(std::string_view name) {
    if (starts_with(name, "u")) name.remove_prefix(1);
    if (!starts_with(name, "int") || !ends_with(name, "_t")) return false;
    name = name.substr(3, name.size() - 5);   // between "int" and "_t"
    if (name == "ptr" || name == "max") return true;
    if (starts_with(name, "_fast")) name.remove_prefix(5);
    else if (starts_with(name, "_least")) name.remove_prefix(6);
    return name == "8" || name == "16" || name == "32" || name == "64";
}

// The C library's types: in <cstddef> and friends, global and in std.
std::string c_type_header(std::string_view name) {
    if (name == "size_t" || name == "ptrdiff_t") return "cstddef";
    if (name == "FILE") return "cstdio";
    if (is_fixed_width_int(name)) return "cstdint";
    return {};
}

// qint64, quint8, qreal: Qt's typedefs live in <QtGlobal>.
bool is_qt_typedef(std::string_view name) {
    static const std::set<std::string_view> names = {"qint8",  "qint16",  "qint32",   "qint64",  "quint8",  "quint16", "quint32",
                                                     "quint64", "qlonglong", "qulonglong", "qreal", "qsizetype", "qintptr", "quintptr", "qptrdiff"};
    return names.count(name) != 0;
}

// QString, QObject, QVariant: Q followed by a capital, and nothing qualified.
bool is_qt_class(std::string_view name) {
    return name.size() >= 2 && name[0] == 'Q' && std::isupper(static_cast<unsigned char>(name[1])) != 0 &&
           std::all_of(name.begin(), name.end(), [](char c) { return is_ident_char(c); });
}

std::string_view strip_global(std::string_view name) {
    if (starts_with(name, "::")) name.remove_prefix(2);
    return name;
}

// ---- mentioned names -------------------------------------------------------------------------------------------

bool is_keyword(const std::string& word) {
    static const std::set<std::string> words = {
        "const",   "volatile", "unsigned", "signed", "int",  "char",   "short",   "long",   "float",  "double",   "bool",
        "void",    "wchar_t",  "char8_t",  "char16_t", "char32_t", "auto", "struct", "class", "union", "enum", "typename",
        "template", "noexcept", "decltype", "constexpr", "mutable", "register", "true", "false", "nullptr"};
    return words.count(word) != 0;
}

// ---- default construction -------------------------------------------------------------------------------------

std::vector<std::string> split_top_level(std::string_view args) {
    std::vector<std::string> out;
    int depth = 0;
    std::string current;
    for (char c : args) {
        if (c == '<' || c == '(' || c == '[') ++depth;
        if (c == '>' || c == ')' || c == ']') --depth;
        if (c == ',' && depth == 0) {
            out.push_back(normalize_spelling(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) out.push_back(normalize_spelling(current));
    return out;
}

bool is_fundamental_words(std::string_view base) {
    static const std::set<std::string> words = {"bool",   "char",     "signed",   "unsigned", "short",    "int",
                                                "long",   "float",    "double",   "wchar_t",  "char8_t",  "char16_t",
                                                "char32_t"};
    std::string word;
    bool any = false;
    for (std::size_t i = 0; i <= base.size(); ++i) {
        if (i < base.size() && base[i] != ' ') {
            word += base[i];
            continue;
        }
        if (word.empty()) continue;
        if (words.count(word) == 0) return false;
        any = true;
        word.clear();
    }
    return any;
}

bool matches_class(const std::string& written, const std::string& qualified) {
    return qualified == written || ends_with(qualified, "::" + written);
}

// Can `{}` build this class without naming a constructor? `return {};` is
// copy-list-initialisation, which an `explicit` default constructor refuses;
// `T name{};` is direct-list-initialisation, which does not.
bool brace_constructible(const Class& c, bool allow_explicit) {
    bool any_ctor = false;
    for (const Method& m : c.methods) {
        if (m.is_pure) return false;
        if (m.kind != MethodKind::Constructor) continue;
        any_ctor = true;
        if (m.is_deleted || m.access != Access::Public || (m.is_explicit && !allow_explicit)) continue;
        const bool all_defaulted = std::all_of(m.params.begin(), m.params.end(), [](const Param& p) { return p.default_value.has_value(); });
        if (all_defaulted) return true;
    }
    return !any_ctor;
}

// The class a written name refers to, nested classes included. A nested class
// is found as `Outer::Inner` (or by any suffix of its qualified name) and only
// if it is public.
const Class* find_class_like(const Class& c, const std::string& qualified, const std::string& written) {
    if (matches_class(written, qualified)) return &c;
    for (const Class& n : c.nested_classes) {
        if (n.nested_access != Access::Public) continue;
        if (const Class* found = find_class_like(n, qualified + "::" + n.name, written)) return found;
    }
    return nullptr;
}

// Is `written` a public enum nested in `c` (or in a public class nested in it)?
bool has_nested_enum(const Class& c, const std::string& qualified, const std::string& written) {
    for (const std::string& e : c.nested_enums) {
        if (matches_class(written, qualified + "::" + e)) return true;
    }
    for (const Class& n : c.nested_classes) {
        if (n.nested_access == Access::Public && has_nested_enum(n, qualified + "::" + n.name, written)) return true;
    }
    return false;
}

const Class* find_class_in(const std::vector<const Model*>& models, const std::string& written) {
    for (const Model* model : models) {
        if (model == nullptr) continue;
        for (const Class& c : model->classes) {
            if (const Class* found = find_class_like(c, qualified_name(c.ns, c.name), written)) return found;
        }
    }
    return nullptr;
}

bool value_returnable(const std::string& base, const std::vector<const Model*>& models, const std::vector<std::string>& user_types,
                      bool allow_explicit) {
    const std::string written(strip_global(base));
    for (const std::string& u : user_types) {
        if (std::string(strip_global(u)) == written) return true;
    }
    if (is_fundamental_words(written)) return true;

    const std::size_t angle = written.find('<');
    const std::string name = written.substr(0, angle);
    std::vector<std::string> args;
    if (angle != std::string::npos && ends_with(written, ">")) args = split_top_level(written.substr(angle + 1, written.size() - angle - 2));

    const bool in_std = starts_with(name, "std::");
    const std::string bare = in_std ? name.substr(5) : name;
    if (!c_type_header(bare).empty() && bare != "FILE") return true;
    if (is_qt_typedef(name) || is_qt_class(name)) return true;

    if (in_std) {
        if (starts_with(bare, "chrono::")) return true;
        if (bare == "nullptr_t" || bare == "byte") return true;
        if (bare == "pair" || bare == "tuple") {
            return std::all_of(args.begin(), args.end(),
                               [&](const std::string& a) { return value_returnable(a, models, user_types, allow_explicit); });
        }
        if (bare == "array") return !args.empty() && value_returnable(args.front(), models, user_types, allow_explicit);
        // Containers, strings, smart pointers, function, optional: all
        // default-constructible whatever they hold.
        static const std::set<std::string> always = {
            "basic_string", "bitset", "deque", "function", "list", "map", "multimap", "multiset", "optional", "queue", "set",
            "shared_ptr", "stack", "string", "string_view", "u16string", "u32string", "unique_ptr", "unordered_map",
            "unordered_multimap", "unordered_multiset", "unordered_set", "vector", "weak_ptr", "wstring", "atomic"};
        return always.count(bare) != 0;
    }

    for (const Model* model : models) {
        if (model == nullptr) continue;
        for (const Enum& e : model->enums) {
            if (matches_class(name, qualified_name(e.ns, e.name))) return true;
        }
    }
    for (const Model* model : models) {
        if (model == nullptr) continue;
        for (const Class& c : model->classes) {
            if (has_nested_enum(c, qualified_name(c.ns, c.name), name)) return true;
        }
    }
    if (const Class* c = find_class_in(models, name)) return brace_constructible(*c, allow_explicit);
    return false;
}

}  // namespace

std::string system_include_for(std::string_view qualified) {
    const std::string_view name = strip_global(qualified);
    const bool in_std = starts_with(name, "std::");
    const std::string_view bare = in_std ? name.substr(5) : name;

    const std::string c = c_type_header(bare);
    if (!c.empty()) return c;
    if (in_std) {
        if (starts_with(bare, "chrono::")) return "chrono";
        const Entry* e = find_entry(bare);
        return e != nullptr ? std::string(e->header) : std::string();
    }
    if (is_qt_typedef(bare)) return "QtGlobal";
    if (is_qt_class(bare)) return std::string(bare);
    return {};
}

std::vector<std::string> mentioned_names(std::string_view spelling) {
    std::vector<std::string> out;
    std::size_t i = 0;
    const std::size_t n = spelling.size();
    while (i < n) {
        const char c = spelling[i];
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            // 3u, 0x1F, 1.5f, 1'000: one token, none of it a name.
            while (i < n && (is_ident_char(spelling[i]) || spelling[i] == '.' || spelling[i] == '\'')) ++i;
            continue;
        }
        const bool global = c == ':' && i + 2 < n && spelling[i + 1] == ':' && is_ident_start(spelling[i + 2]);
        if (!is_ident_start(c) && !global) {
            ++i;
            continue;
        }
        if (global) i += 2;
        const std::size_t begin = i;
        std::size_t end = i;
        for (;;) {
            while (end < n && is_ident_char(spelling[end])) ++end;
            if (end + 2 < n && spelling[end] == ':' && spelling[end + 1] == ':' && is_ident_start(spelling[end + 2])) {
                end += 2;
                continue;
            }
            break;
        }
        const std::string name(spelling.substr(begin, end - begin));
        i = end;
        if (is_keyword(name)) continue;
        if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
    }
    return out;
}

bool is_default_returnable(const Type& type, const std::vector<const Model*>& models, const std::vector<std::string>& user_types) {
    if (type.empty() || type.is_void() || type.ref != RefKind::None) return false;
    if (type.pointer_depth > 0) return true;
    return value_returnable(type.base, models, user_types, false);
}

bool is_default_constructible(const Type& type, const std::vector<const Model*>& models, const std::vector<std::string>& user_types) {
    if (type.empty() || type.is_void()) return false;
    if (type.pointer_depth > 0) return true;
    return value_returnable(type.base, models, user_types, true);
}

}  // namespace mt::ir
