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

// `#include <...>` lines, then `#include "..."`, each sorted and once.
nlohmann::json include_lines(const std::vector<ir::Include>& includes);

}  // namespace mt::backend

#endif  // MT_BACKEND_TEST_RENDER_H
