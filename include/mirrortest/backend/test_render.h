// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The part of spelling a test that is the same in every framework: a body is a
// list of statements, one per line, indented four spaces, and only the
// placeholder that keeps an unwritten test red differs (FAIL(), QFAIL()).
// Shared by backend/gtest_backend and backend/qtest_backend.

#ifndef MT_BACKEND_TEST_RENDER_H
#define MT_BACKEND_TEST_RENDER_H

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mirrortest/ir/model.h"

namespace mt::backend {

// What goes around the message of a placeholder: `FAIL() << "` and `";` make
// FAIL() << "TODO(mt): ...";
struct PlaceholderStyle {
    std::string before;
    std::string after;
};

// One statement as its line, indented. Blank is an empty line, not a line of
// spaces; a commented-out statement is `// text;`; the text of a message is
// escaped as a string literal.
std::string statement_line(const ir::Stmt& statement, const PlaceholderStyle& placeholder);

// The lines of a body, one per statement. A Data statement is replaced by `data_lines` (already
// indented): how the arguments of a case that runs over a table are read into locals is the
// framework's -- GetParam() for GoogleTest, QFETCH for Qt Test.
std::vector<std::string> body_lines(const std::vector<ir::Stmt>& body, const PlaceholderStyle& placeholder,
                                    const std::vector<std::string>& data_lines);

// `lines` between `#if <condition>` and `#endif`, once for each condition of `outer` and then of `inner` (outermost first): the test
// of a member that exists only in some configurations is compiled only in those (design 6.5). The lines themselves, if there
// is no condition.
nlohmann::json guarded_lines(const nlohmann::json& lines, const std::vector<std::string>& outer,
                             const std::vector<std::string>& inner = {});

// `#include <...>` lines, then `#include "..."`, each sorted and once.
nlohmann::json include_lines(const std::vector<ir::Include>& includes);

}  // namespace mt::backend

#endif  // MT_BACKEND_TEST_RENDER_H
