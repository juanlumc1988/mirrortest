// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The test frontend: a test file in, facts out (docs/design.md, section 7).
//
// The input does not compile -- it uses classes that do not exist yet -- so
// this is purely syntactic and leans on one thing only: what the test itself
// declares. `Foo foo(3, "abc");` says `foo` is a Foo; `foo.size()` is then a
// call to Foo::size on a non-const object; `EXPECT_EQ(foo.size(), 3u)` says
// what it returned. From such chains of declared types the frontend records
// facts (frontend/facts.h) and nothing else: it does not decide which names
// are classes to generate or what types they should have.
//
// What it reads, in order:
//   1. the file's own classes -- fixtures and helpers -- through the code
//      frontend; they give TEST_F bodies their members and are never generated;
//   2. every function body: TEST/TEST_F/TEST_P, fixture methods (inline or
//      defined outside the class), helper functions and lambdas;
//   3. inside a body, the statements and expressions, with a symbol table of
//      the declared locals, parameters and fixture members.
//
// GoogleTest only for now; the QTest recogniser is plan phase 5.

#ifndef MT_FRONTEND_TEST_FRONTEND_H
#define MT_FRONTEND_TEST_FRONTEND_H

#include "mirrortest/core/config.h"
#include "mirrortest/core/diagnostics.h"
#include "mirrortest/core/source.h"
#include "mirrortest/frontend/facts.h"
#include "mirrortest/syntax/preprocess.h"

namespace mt::frontend {

struct TestFrontendOptions {
    core::CxxStd       standard = core::CxxStd::Cxx17;   // for literal warnings
    syntax::MacroTable macros;
};

// Preprocesses (Qt macros), parses and reads one test file.
TestFileFacts read_test_file(const core::SourceFile& source, const TestFrontendOptions& options,
                             core::Diagnostics& diagnostics);

}  // namespace mt::frontend

#endif  // MT_FRONTEND_TEST_FRONTEND_H
