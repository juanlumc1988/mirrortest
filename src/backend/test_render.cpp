// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/backend/test_render.h"

#include <set>
#include <string>
#include <vector>

#include "mirrortest/backend/cxx_format.h"

namespace mt::backend {

namespace {

const char* const kIndent = "    ";

}  // namespace

std::string statement_line(const ir::Stmt& s, const PlaceholderStyle& placeholder) {
    switch (s.kind) {
        case ir::Stmt::Kind::Blank:
            return {};
        case ir::Stmt::Kind::Comment:
            return std::string(kIndent) + "// " + s.text;
        case ir::Stmt::Kind::Code:
            return std::string(kIndent) + (s.commented_out ? "// " : "") + s.text + ";";
        case ir::Stmt::Kind::StaticAssert:
            return std::string(kIndent) + (s.commented_out ? "// " : "") + "static_assert(" + s.text + ", \"" +
                   escape_literal(s.message) + "\");";
        case ir::Stmt::Kind::Placeholder:
            // Both frameworks return from the test: what follows is compiled, never run.
            return std::string(kIndent) + placeholder.before + escape_literal(s.text) + placeholder.after;
        case ir::Stmt::Kind::Data:
            return {};   // not one line: body_lines() spells it
    }
    return {};
}

std::vector<std::string> body_lines(const std::vector<ir::Stmt>& body, const PlaceholderStyle& placeholder,
                                    const std::vector<std::string>& data_lines) {
    std::vector<std::string> out;
    for (const ir::Stmt& s : body) {
        if (s.kind == ir::Stmt::Kind::Data) {
            out.insert(out.end(), data_lines.begin(), data_lines.end());
        } else {
            out.push_back(statement_line(s, placeholder));
        }
    }
    return out;
}

nlohmann::json include_lines(const std::vector<ir::Include>& includes) {
    std::set<std::string> system;
    std::set<std::string> local;
    for (const ir::Include& i : includes) (i.is_system ? system : local).insert(i.path);
    nlohmann::json lines = nlohmann::json::array();
    for (const std::string& s : system) lines.push_back("#include <" + s + ">");
    for (const std::string& s : local) lines.push_back("#include \"" + s + "\"");
    return lines;
}

}  // namespace mt::backend
