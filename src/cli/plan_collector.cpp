// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/cli/plan_collector.h"

namespace mt::cli {

void PlanCollector::add(const std::string& input, std::vector<core::OutputFile> files, const std::string& advice,
                        core::Diagnostics& diagnostics) {
    for (core::OutputFile& file : files) {
        const auto seen = origin_.find(file.path);
        if (seen == origin_.end()) {
            origin_[file.path] = {input, plan_.files.size()};
            plan_.files.push_back(std::move(file));
        } else if (plan_.files[seen->second.second].content != file.content) {
            ir::SourceLoc where;
            where.file = input;
            diagnostics.error(core::code::unsupported,
                              "'" + seen->second.first + "' and '" + input + "' both generate '" + file.path +
                                  "' with different content; " + advice,
                              where);
        }
    }
}

}  // namespace mt::cli
