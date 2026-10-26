// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The files of several inputs, as one plan. A run over many inputs may reach
// the same path from more than one of them (two tests that include the same
// header, two headers that both declare a `Foo`); that is fine when they
// produce the same bytes and an error when they do not, since one of the two
// would silently win.

#ifndef MT_CLI_PLAN_COLLECTOR_H
#define MT_CLI_PLAN_COLLECTOR_H

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "mirrortest/core/diagnostics.h"
#include "mirrortest/core/output.h"

namespace mt::cli {

class PlanCollector {
public:
    // Adds the files `input` generated. A path that an earlier input generated
    // too, with the same content, is kept once; with different content it is an
    // mt-unsupported error: "'a' and 'b' both generate 'p' with different
    // content; <advice>". `advice` says what to do instead.
    void add(const std::string& input, std::vector<core::OutputFile> files, const std::string& advice,
             core::Diagnostics& diagnostics);

    core::OutputPlan take() { return std::move(plan_); }

private:
    core::OutputPlan plan_;
    std::map<std::string, std::pair<std::string, std::size_t>> origin_;   // path -> (input, index in the plan)
};

}  // namespace mt::cli

#endif  // MT_CLI_PLAN_COLLECTOR_H
