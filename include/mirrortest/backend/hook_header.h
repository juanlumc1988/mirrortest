// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The header that the `hook` stub policy needs (docs/design.md, section 8.3).
// A stub of that policy calls `mt_not_implemented("<signature>")` instead of
// throwing, and what that does can be changed while the program runs: a handler
// that makes the running test fail and returns lets the other tests go on, which
// QTest cannot do after an exception (R26).
//
// One header and nothing to link: every function is `inline`, and the handler
// lives in a function-local static, which C++11 initialises once and
// thread-safely. It is plain text built here, not a template: there is nothing in
// it for a project to restyle.

#ifndef MT_BACKEND_HOOK_HEADER_H
#define MT_BACKEND_HOOK_HEADER_H

#include <string>

#include "mirrortest/core/config.h"

namespace mt::backend {

// The file the stubs include and where it goes, below the header directory.
inline constexpr const char* kHookHeaderName = "mt_not_implemented.h";

// Its text. The default handler throws a `std::logic_error`, or -- with
// `no_exceptions` or `no_stl`, which have nothing to throw -- prints the signature
// with <cstdio> and aborts. Every test of a run, and of a later run, needs this same
// file, so nothing in it depends on the test that asked for it (it used to name the
// test in its first line, which made two tests in one run an error and a second run
// leave a `.new` for a comment). Ends with a newline, uses LF.
std::string hook_header(const core::Config& config);

}  // namespace mt::backend

#endif  // MT_BACKEND_HOOK_HEADER_H
