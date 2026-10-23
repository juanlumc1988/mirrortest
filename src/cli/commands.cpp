// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The two commands. Until their frontends and backends exist (docs/plan.md,
// phases 2 and 4) each one reports that, as an error with a diagnostic code,
// so scripts see a failure rather than a silent "nothing to do".

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

int run_gen_impl(const Options& options, const Env& env) {
    return not_implemented(options, env, "gen-impl");
}

int run_gen_tests(const Options& options, const Env& env) {
    return not_implemented(options, env, "gen-tests");
}

}  // namespace mt::cli
