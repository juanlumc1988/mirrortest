// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/frontend/facts.h"

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
        }
    }
    return evidence;
}

}  // namespace mt::frontend
