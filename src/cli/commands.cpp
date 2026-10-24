// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// gen-tests. Until its frontend and backend exist (docs/plan.md, phase 4) it
// reports that, as an error with a diagnostic code, so scripts see a failure
// rather than a silent "nothing to do". gen-impl is in gen_impl.cpp.

#include "mirrortest/cli/run.h"

namespace mt::cli {

namespace {

int not_implemented(const Options& options, const Env& env, const char* command) {
    core::Diagnostics diagnostics;
    diagnostics.error(core::code::unsupported,
                      std::string(command) + " is not implemented yet (see docs/plan.md)");
    return finish_run(options, env, diagnostics, nullptr);
}

}  // namespace

int run_gen_tests(const Options& options, const Env& env) {
    return not_implemented(options, env, "gen-tests");
}

}  // namespace mt::cli
