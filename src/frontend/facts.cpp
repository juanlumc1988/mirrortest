// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/frontend/facts.h"

#include <set>
#include <sstream>

#include "mirrortest/ir/type.h"

namespace mt::frontend {

std::string TypeName::qualified() const {
    return ir::qualified_name(scope, name);
}

TypeName parse_type_name(std::string_view spelling) {
    const std::string text = ir::normalize_spelling(spelling);
    std::string_view rest = text;
    if (rest.compare(0, 2, "::") == 0) {
        rest.remove_prefix(2);
    }

    // Split at the "::" that sit outside any <...>: `Box<std::string>::create`
    // is the scope Box and the name create, and the "std" inside the arguments
    // is nobody's scope.
    std::vector<std::string_view> segments;
    int depth = 0;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '<') {
            ++depth;
        } else if (rest[i] == '>') {
            if (depth > 0) --depth;
        } else if (depth == 0 && rest.compare(i, 2, "::") == 0) {
            segments.push_back(rest.substr(begin, i - begin));
            begin = i + 2;
            ++i;
        }
    }
    segments.push_back(rest.substr(begin));

    TypeName out;
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        const std::size_t open = segments[i].find('<');
        out.scope.emplace_back(open == std::string_view::npos ? segments[i] : segments[i].substr(0, open));
    }

    std::string_view last = segments.back();
    const std::size_t open = last.find('<');
    if (open != std::string_view::npos) {
        const std::size_t close = last.rfind('>');
        if (close != std::string_view::npos && close > open) {
            out.template_args = std::string(last.substr(open + 1, close - open - 1));
        }
        last = last.substr(0, open);
    }
    out.name = std::string(last);
    return out;
}

bool is_fundamental_name(std::string_view spelling) {
    static const std::set<std::string> words = {
        "bool", "char", "signed", "unsigned", "short", "int", "long", "float", "double", "void",
        "wchar_t", "char8_t", "char16_t", "char32_t"};
    std::istringstream in{std::string(spelling)};
    std::string word;
    bool any = false;
    while (in >> word) {
        any = true;
        if (words.count(word) == 0) return false;
    }
    return any;
}

bool is_smart_pointer(const TypeName& n) {
    const bool std_scope = n.scope.empty() || (n.scope.size() == 1 && n.scope[0] == "std");
    if (std_scope && (n.name == "unique_ptr" || n.name == "shared_ptr")) return true;
    return n.scope.empty() && (n.name == "QScopedPointer" || n.name == "QSharedPointer" || n.name == "QPointer");
}

std::string first_template_argument(const std::string& args) {
    int depth = 0;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == '<' || args[i] == '(') ++depth;
        else if (args[i] == '>' || args[i] == ')') --depth;
        else if (args[i] == ',' && depth == 0) return ir::normalize_spelling(args.substr(0, i));
    }
    return ir::normalize_spelling(args);
}

std::optional<TypeName> class_name_of(const ir::Type& t) {
    const std::string& base = t.base;
    if (base.empty() || base == "auto" || base == "..." || base.find('(') != std::string::npos ||
        base.find('[') != std::string::npos || is_fundamental_name(base)) {
        return std::nullopt;
    }
    TypeName name = parse_type_name(base);
    if (name.name.empty()) return std::nullopt;
    return name;
}

std::optional<Receiver> receiver_of_type(const ir::Type& t, bool arrow) {
    const std::optional<TypeName> name = class_name_of(t);
    if (!name) return std::nullopt;

    if (is_smart_pointer(*name)) {
        if (t.pointer_depth != 0) return std::nullopt;
        if (!arrow) return Receiver{*name, t.is_const};           // p.reset(): the pointer itself
        const std::string inner = first_template_argument(name->template_args);
        if (inner.empty()) return std::nullopt;
        const bool is_const = inner.compare(0, 6, "const ") == 0;
        const TypeName pointee = parse_type_name(is_const ? inner.substr(6) : inner);
        if (pointee.name.empty() || is_fundamental_name(pointee.name)) return std::nullopt;
        return Receiver{pointee, is_const};
    }
    if (arrow ? t.pointer_depth != 1 : t.pointer_depth != 0) return std::nullopt;
    return Receiver{*name, t.is_const};
}

FrameworkEvidence detect_framework(const TestFileFacts& facts) {
    FrameworkEvidence evidence;
    for (const ir::Include& inc : facts.includes) {
        if (inc.path.rfind("gtest/", 0) == 0 || inc.path.rfind("gmock/", 0) == 0) {
            evidence.gtest = true;
        }
        if (inc.path == "QtTest" || inc.path == "QTest" || inc.path.rfind("QtTest/", 0) == 0 ||
            inc.path.rfind("QTest", 0) == 0) {
            evidence.qtest = true;
        }
    }
    for (const TestCaseInfo& t : facts.tests) {
        if (t.kind == "TEST" || t.kind == "TEST_F" || t.kind == "TEST_P") {
            evidence.gtest = true;
        } else if (t.kind == "SLOT") {
            evidence.qtest = true;
        }
    }
    if (facts.qtest_main) evidence.qtest = true;
    return evidence;
}

}  // namespace mt::frontend
