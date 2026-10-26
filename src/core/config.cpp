// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/core/config.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

#include "mirrortest/core/naming.h"
#include "mirrortest/core/source.h"
#include "mirrortest/ir/type.h"

namespace mt::core {

namespace {

using json = nlohmann::json;

// ---- enumerations and their spellings -----------------------------------------

template <class E>
struct Choice {
    E           value;
    const char* name;
};

const Choice<CxxStd> kStds[] = {
    {CxxStd::Cxx11, "c++11"}, {CxxStd::Cxx14, "c++14"}, {CxxStd::Cxx17, "c++17"},
    {CxxStd::Cxx20, "c++20"}, {CxxStd::Cxx23, "c++23"},
};
const Choice<GuardStyle> kGuards[] = {
    {GuardStyle::Ifndef, "ifndef"}, {GuardStyle::PragmaOnce, "pragma-once"},
};
const Choice<StubPolicy> kStubPolicies[] = {
    {StubPolicy::Throw, "throw"}, {StubPolicy::Abort, "abort"}, {StubPolicy::Default, "default"},
};
const Choice<Placeholder> kPlaceholders[] = {
    {Placeholder::Fail, "fail"}, {Placeholder::Skip, "skip"},
};
const Choice<AutoPolicy> kAutoPolicies[] = {
    {AutoPolicy::Error, "error"}, {AutoPolicy::Todo, "todo"},
};

template <class E, std::size_t N>
const char* name_of(const Choice<E> (&table)[N], E value) {
    for (const Choice<E>& c : table) {
        if (c.value == value) {
            return c.name;
        }
    }
    return table[0].name;
}

template <class E, std::size_t N>
std::optional<E> find_choice(const Choice<E> (&table)[N], std::string_view text) {
    for (const Choice<E>& c : table) {
        if (text == c.name) {
            return c.value;
        }
    }
    return std::nullopt;
}

template <class E, std::size_t N>
std::string choice_list(const Choice<E> (&table)[N]) {
    std::string out;
    for (const Choice<E>& c : table) {
        if (!out.empty()) {
            out += ", ";
        }
        out += c.name;
    }
    return out;
}

// ---- lexical helpers (ASCII, locale-independent) --------------------------------

bool is_blank(std::string_view s) {
    return std::all_of(s.begin(), s.end(), [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    });
}

// ---- reading -------------------------------------------------------------------

class Reader {
public:
    Reader(const std::string& origin, Diagnostics& diagnostics)
        : origin_(origin), diagnostics_(diagnostics) {}

    bool failed() const { return failed_; }

    void error(const std::string& message) {
        failed_ = true;
        ir::SourceLoc where;
        where.file = origin_;
        diagnostics_.error(code::config, message, where);
    }

    void read_root(const json& root, Config& c);

private:
    std::string key_error(const std::string& path, const std::string& what) const {
        return "configuration key '" + path + "' " + what;
    }
    void bad_type(const std::string& path, const char* expected) {
        error(key_error(path, std::string("must be ") + expected));
    }
    void unknown_key(const std::string& path) { error("unknown configuration key '" + path + "'"); }

    bool read_string(const json& v, const std::string& path, std::string& out) {
        if (!v.is_string()) {
            bad_type(path, "a string");
            return false;
        }
        out = v.get<std::string>();
        return true;
    }

    bool read_bool(const json& v, const std::string& path, bool& out) {
        if (!v.is_boolean()) {
            bad_type(path, "true or false");
            return false;
        }
        out = v.get<bool>();
        return true;
    }

    template <class E, std::size_t N>
    void read_choice(const json& v, const std::string& path, const Choice<E> (&table)[N], E& out) {
        std::string text;
        if (!read_string(v, path, text)) {
            return;
        }
        const std::optional<E> found = find_choice(table, text);
        if (!found) {
            error(key_error(path, "must be one of: " + choice_list(table) + " (got '" + text + "')"));
            return;
        }
        out = *found;
    }

    // A type spelling, stored normalised.
    bool read_type(const json& v, const std::string& path, std::string& out) {
        std::string text;
        if (!read_string(v, path, text)) {
            return false;
        }
        text = ir::normalize_spelling(text);
        if (text.empty()) {
            error(key_error(path, "must not be empty"));
            return false;
        }
        out = std::move(text);
        return true;
    }

    // A list of strings, each checked by `valid` (which also describes what it
    // wanted, for the message).
    template <class Check>
    void read_list(const json& v, const std::string& path, std::vector<std::string>& out,
                   const char* what, Check valid) {
        if (!v.is_array()) {
            bad_type(path, "a list");
            return;
        }
        std::vector<std::string> items;
        for (std::size_t i = 0; i < v.size(); ++i) {
            const std::string where = path + "[" + std::to_string(i) + "]";
            if (!v[i].is_string()) {
                bad_type(where, "a string");
                continue;
            }
            std::string text = v[i].get<std::string>();
            if (!valid(text)) {
                error(key_error(where, std::string("must be ") + what + " (got '" + text + "')"));
                continue;
            }
            items.push_back(std::move(text));
        }
        out = std::move(items);
    }

    void read_type_list(const json& v, const std::string& path, std::vector<std::string>& out) {
        if (!v.is_array()) {
            bad_type(path, "a list");
            return;
        }
        std::vector<std::string> items;
        for (std::size_t i = 0; i < v.size(); ++i) {
            std::string type;
            if (read_type(v[i], path + "[" + std::to_string(i) + "]", type)) {
                items.push_back(std::move(type));
            }
        }
        out = std::move(items);
    }

    void read_pattern(const json& v, const std::string& path, const char* placeholder,
                      std::string& out);
    void read_naming(const json& v, Naming& naming);
    void read_gtest(const json& v, GtestConfig& gtest);
    void read_type_map(const json& v, std::map<std::string, std::string>& out);
    void read_overrides(const json& v, std::map<std::string, MemberOverride>& out);
    void read_member_override(const json& v, const std::string& path, MemberOverride& out);
    void read_instantiations(const json& v,
                             std::map<std::string, std::vector<std::string>>& out);
    void read_file_header(const json& v, std::string& out);
    void read_default_namespace(const json& v, std::string& out);

    const std::string& origin_;
    Diagnostics&       diagnostics_;
    bool               failed_ = false;
};

// A name or file pattern such as "{stem}_test.cpp". Exactly one placeholder is
// known for each pattern, and it must appear: a pattern without it would send
// every class to the same file or give every suite the same name.
void Reader::read_pattern(const json& v, const std::string& path, const char* placeholder,
                          std::string& out) {
    std::string text;
    if (!read_string(v, path, text)) {
        return;
    }
    const std::string wanted = std::string("{") + placeholder + "}";
    bool has_wanted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{') {
            const std::size_t close = text.find('}', i);
            if (close == std::string::npos) {
                error(key_error(path, "has an unclosed '{' (got '" + text + "')"));
                return;
            }
            const std::string token = text.substr(i, close - i + 1);
            if (token != wanted) {
                error(key_error(path, "uses unknown placeholder " + token + "; only " + wanted +
                                          " is available here"));
                return;
            }
            has_wanted = true;
            i = close;
        } else if (text[i] == '}') {
            error(key_error(path, "has an unmatched '}' (got '" + text + "')"));
            return;
        }
    }
    if (!has_wanted) {
        error(key_error(path, "must contain " + wanted + " (got '" + text + "')"));
        return;
    }
    out = std::move(text);
}

void Reader::read_naming(const json& v, Naming& naming) {
    if (!v.is_object()) {
        bad_type("naming", "an object");
        return;
    }
    for (auto it = v.begin(); it != v.end(); ++it) {
        const std::string& key = it.key();
        const std::string path = "naming." + key;
        if (key == "header")           read_pattern(it.value(), path, "stem", naming.header);
        else if (key == "source")      read_pattern(it.value(), path, "stem", naming.source);
        else if (key == "gtest_file")  read_pattern(it.value(), path, "stem", naming.gtest_file);
        else if (key == "qtest_file")  read_pattern(it.value(), path, "stem", naming.qtest_file);
        else if (key == "suite")       read_pattern(it.value(), path, "Class", naming.suite);
        else if (key == "qtest_class") read_pattern(it.value(), path, "Class", naming.qtest_class);
        else if (key == "mock_file")   read_pattern(it.value(), path, "stem", naming.mock_file);
        else if (key == "mock_class")  read_pattern(it.value(), path, "Class", naming.mock_class);
        else                           unknown_key(path);
    }
}

void Reader::read_gtest(const json& v, GtestConfig& gtest) {
    if (!v.is_object()) {
        bad_type("gtest", "an object");
        return;
    }
    for (auto it = v.begin(); it != v.end(); ++it) {
        const std::string& key = it.key();
        if (key == "fixture")    read_bool(it.value(), "gtest.fixture", gtest.fixture);
        else if (key == "mocks") read_bool(it.value(), "gtest.mocks", gtest.mocks);
        else                     unknown_key("gtest." + key);
    }
}

void Reader::read_type_map(const json& v, std::map<std::string, std::string>& out) {
    if (!v.is_object()) {
        bad_type("type_map", "an object");
        return;
    }
    std::map<std::string, std::string> map;
    for (auto it = v.begin(); it != v.end(); ++it) {
        const std::string path = "type_map." + it.key();
        const std::string from = ir::normalize_spelling(it.key());
        std::string to;
        if (from.empty()) {
            error(key_error(path, "must not be an empty type name"));
            continue;
        }
        if (!read_type(it.value(), path, to)) {
            continue;
        }
        if (!map.emplace(from, to).second) {
            error(key_error(path, "repeats '" + from + "' (type names are compared normalised)"));
        }
    }
    out = std::move(map);
}

void Reader::read_member_override(const json& v, const std::string& path, MemberOverride& out) {
    if (!v.is_object()) {
        bad_type(path, "an object");
        return;
    }
    MemberOverride m;
    for (auto it = v.begin(); it != v.end(); ++it) {
        const std::string& key = it.key();
        const std::string where = path + "." + key;
        bool flag = false;
        if (key == "returns") {
            std::string type;
            if (read_type(it.value(), where, type)) {
                m.returns = std::move(type);
            }
        } else if (key == "params") {
            if (!it.value().is_object()) {
                bad_type(where, "an object mapping a 1-based position to a type");
                continue;
            }
            for (auto p = it.value().begin(); p != it.value().end(); ++p) {
                const std::string& index = p.key();
                const std::string param_path = where + "." + index;
                const bool numeric = !index.empty() && index.size() <= 3 &&
                                     std::all_of(index.begin(), index.end(),
                                                 [](char c) { return c >= '0' && c <= '9'; }) &&
                                     index[0] != '0';
                if (!numeric) {
                    error(key_error(param_path,
                                    "is not a parameter position (use 1, 2, ... without leading zeros)"));
                    continue;
                }
                std::string type;
                if (read_type(p.value(), param_path, type)) {
                    m.params[static_cast<unsigned>(std::stoul(index))] = std::move(type);
                }
            }
        } else if (key == "const") {
            if (read_bool(it.value(), where, flag)) m.is_const = flag;
        } else if (key == "static") {
            if (read_bool(it.value(), where, flag)) m.is_static = flag;
        } else if (key == "virtual") {
            if (read_bool(it.value(), where, flag)) m.is_virtual = flag;
        } else if (key == "noexcept") {
            if (read_bool(it.value(), where, flag)) m.is_noexcept = flag;
        } else {
            unknown_key(where);
        }
    }
    out = std::move(m);
}

void Reader::read_overrides(const json& v, std::map<std::string, MemberOverride>& out) {
    if (!v.is_object()) {
        bad_type("overrides", "an object");
        return;
    }
    std::map<std::string, MemberOverride> map;
    for (auto it = v.begin(); it != v.end(); ++it) {
        const std::string path = "overrides." + it.key();
        if (!is_qualified_name(it.key(), 2)) {
            error(key_error(path, "must name a member as Class::member or ns::Class::member"));
            continue;
        }
        MemberOverride m;
        read_member_override(it.value(), path, m);
        map[it.key()] = std::move(m);
    }
    out = std::move(map);
}

void Reader::read_instantiations(const json& v,
                                 std::map<std::string, std::vector<std::string>>& out) {
    if (!v.is_object()) {
        bad_type("template_instantiations", "an object");
        return;
    }
    std::map<std::string, std::vector<std::string>> map;
    for (auto it = v.begin(); it != v.end(); ++it) {
        const std::string path = "template_instantiations." + it.key();
        if (!is_qualified_name(it.key(), 1)) {
            error(key_error(path, "must name a class template as Box or ns::Box"));
            continue;
        }
        std::vector<std::string> types;
        read_type_list(it.value(), path, types);
        if (it.value().is_array() && it.value().empty()) {
            error(key_error(path, "must list at least one instantiation"));
            continue;
        }
        std::set<std::string> seen;
        for (const std::string& t : types) {
            if (!seen.insert(t).second) {
                error(key_error(path, "lists '" + t + "' twice"));
            }
        }
        map[it.key()] = std::move(types);
    }
    out = std::move(map);
}

// Emitted verbatim at the top of every generated file, so it has to be a
// comment or the output would not compile.
void Reader::read_file_header(const json& v, std::string& out) {
    std::string text;
    if (!read_string(v, "file_header", text)) {
        return;
    }
    std::string normalised;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        normalised += text[i];
    }
    while (!normalised.empty() && (normalised.back() == '\n' || normalised.back() == ' ' ||
                                   normalised.back() == '\t')) {
        normalised.pop_back();
    }
    if (is_blank(normalised)) {
        out.clear();
        return;
    }
    const std::size_t first = normalised.find_first_not_of(" \t\n");
    const std::string_view head = std::string_view(normalised).substr(first, 2);
    if (head != "//" && head != "/*") {
        error(key_error("file_header", "must be a C++ comment (start with // or /*)"));
        return;
    }
    out = std::move(normalised);
}

void Reader::read_default_namespace(const json& v, std::string& out) {
    std::string text;
    if (!read_string(v, "default_namespace", text)) {
        return;
    }
    if (!text.empty() && !is_qualified_name(text, 1)) {
        error(key_error("default_namespace",
                        "must be empty or a namespace such as acme or acme::core (got '" + text + "')"));
        return;
    }
    out = std::move(text);
}

void Reader::read_root(const json& root, Config& c) {
    if (!root.is_object()) {
        error("the configuration must be a JSON object");
        return;
    }
    const auto identifier = [](const std::string& s) { return is_identifier(s); };
    const auto qualified = [](const std::string& s) { return is_qualified_name(s, 1); };

    for (auto it = root.begin(); it != root.end(); ++it) {
        const std::string& key = it.key();
        const json& v = it.value();

        if (key == "std")                              read_choice(v, key, kStds, c.cxx_std);
        else if (key == "default_namespace")           read_default_namespace(v, c.default_namespace);
        else if (key == "naming")                      read_naming(v, c.naming);
        else if (key == "gtest")                       read_gtest(v, c.gtest);
        else if (key == "guard")                       read_choice(v, key, kGuards, c.guard);
        else if (key == "file_header")                 read_file_header(v, c.file_header);
        else if (key == "stub_policy")                 read_choice(v, key, kStubPolicies, c.stub_policy);
        else if (key == "no_exceptions")               read_bool(v, key, c.no_exceptions);
        else if (key == "no_rtti")                     read_bool(v, key, c.no_rtti);
        else if (key == "placeholder")                 read_choice(v, key, kPlaceholders, c.placeholder);
        else if (key == "auto_policy")                 read_choice(v, key, kAutoPolicies, c.auto_policy);
        else if (key == "placeholder_type")            read_type(v, key, c.placeholder_type);
        else if (key == "string_literal_param")        read_type(v, key, c.string_literal_param);
        else if (key == "qt") {
            if (!v.is_number_integer() || (v.get<long long>() != 5 && v.get<long long>() != 6)) {
                error(key_error(key, "must be 5 or 6"));
            } else {
                c.qt = static_cast<int>(v.get<long long>());
            }
        }
        else if (key == "type_map")                    read_type_map(v, c.type_map);
        else if (key == "by_value_types")              read_type_list(v, key, c.by_value_types);
        else if (key == "default_constructible_types") read_type_list(v, key, c.default_constructible_types);
        else if (key == "external_types")              read_list(v, key, c.external_types, "a type name such as Foo or ns::Foo", qualified);
        else if (key == "external_functions")          read_list(v, key, c.external_functions, "a function name such as helper or ns::helper", qualified);
        else if (key == "ignore_macros")               read_list(v, key, c.ignore_macros, "a macro name", identifier);
        else if (key == "overrides")                   read_overrides(v, c.overrides);
        else if (key == "template_instantiations")     read_instantiations(v, c.template_instantiations);
        else                                           unknown_key(key);
    }
}

}  // namespace

// ---- public ----------------------------------------------------------------------

const char* to_string(CxxStd value)         { return name_of(kStds, value); }
const char* to_string(GuardStyle value)     { return name_of(kGuards, value); }
const char* to_string(StubPolicy value)     { return name_of(kStubPolicies, value); }
const char* to_string(Placeholder value)    { return name_of(kPlaceholders, value); }
const char* to_string(AutoPolicy value)     { return name_of(kAutoPolicies, value); }

std::optional<CxxStd> parse_std(std::string_view text) { return find_choice(kStds, text); }
std::optional<GuardStyle> parse_guard_style(std::string_view text) { return find_choice(kGuards, text); }
std::optional<StubPolicy> parse_stub_policy(std::string_view text) { return find_choice(kStubPolicies, text); }
std::optional<Placeholder> parse_placeholder(std::string_view text) { return find_choice(kPlaceholders, text); }
std::optional<AutoPolicy> parse_auto_policy(std::string_view text) { return find_choice(kAutoPolicies, text); }

template <class E, std::size_t N>
std::vector<std::string> names_of(const Choice<E> (&table)[N]) {
    std::vector<std::string> out;
    for (const Choice<E>& c : table) {
        out.emplace_back(c.name);
    }
    return out;
}

std::vector<std::string> std_names() { return names_of(kStds); }
std::vector<std::string> guard_names() { return names_of(kGuards); }
std::vector<std::string> stub_policy_names() { return names_of(kStubPolicies); }
std::vector<std::string> placeholder_names() { return names_of(kPlaceholders); }
std::vector<std::string> auto_policy_names() { return names_of(kAutoPolicies); }

int to_number(CxxStd value) {
    switch (value) {
        case CxxStd::Cxx11: return 11;
        case CxxStd::Cxx14: return 14;
        case CxxStd::Cxx17: return 17;
        case CxxStd::Cxx20: return 20;
        case CxxStd::Cxx23: return 23;
    }
    return 17;
}

StubPolicy effective_stub_policy(const Config& config) {
    if (config.no_exceptions && config.stub_policy == StubPolicy::Throw) {
        return StubPolicy::Abort;
    }
    return config.stub_policy;
}

bool apply_config_json(Config& config, std::string_view text, const std::string& origin,
                       Diagnostics& diagnostics) {
    Reader reader(origin, diagnostics);

    json root;
    try {
        root = json::parse(std::string(text), nullptr, /*allow_exceptions=*/true,
                           /*ignore_comments=*/true);
    } catch (const json::parse_error& e) {
        reader.error(std::string("invalid JSON: ") + e.what());
        return false;
    }

    // Work on a copy so a file with a single error changes nothing.
    Config next = config;
    reader.read_root(root, next);
    if (reader.failed()) {
        return false;
    }
    config = std::move(next);
    return true;
}

bool load_config_file(Config& config, const std::string& path, Diagnostics& diagnostics,
                      const std::filesystem::path& base_dir) {
    const std::optional<SourceFile> file = read_source(path, diagnostics, base_dir);
    if (!file) {
        return false;
    }
    return apply_config_json(config, file->text, path, diagnostics);
}

}  // namespace mt::core
