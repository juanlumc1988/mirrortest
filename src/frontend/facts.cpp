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

    TypeName out;
    std::string_view head = rest;
    const std::size_t open = rest.find('<');
    if (open != std::string_view::npos) {
        head = rest.substr(0, open);
        const std::size_t close = rest.rfind('>');
        if (close != std::string_view::npos && close > open) {
            out.template_args = std::string(rest.substr(open + 1, close - open - 1));
        }
    }

    for (std::size_t sep; (sep = head.find("::")) != std::string_view::npos;) {
        out.scope.emplace_back(head.substr(0, sep));
        head.remove_prefix(sep + 2);
    }
    out.name = std::string(head);
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
