// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The GoogleTest backend: framework-neutral test plans in, test files out
// (docs/design.md, section 9.4).
//
// Same two steps as the implementation backend, so that each can be tested
// without the other:
//
//   build_gtest_views   spells every statement of every case -- the placeholder
//                       as FAIL() or GTEST_SKIP(), a commented-out statement as
//                       a comment -- and returns the files as JSON view models.
//   generate_gtest      renders those through gtest/test_file.inja.
//
// Neither touches the file system. The view model and the template's variables
// are documented in docs/templates.md.

#ifndef MT_BACKEND_GTEST_BACKEND_H
#define MT_BACKEND_GTEST_BACKEND_H

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mirrortest/core/config.h"
#include "mirrortest/core/diagnostics.h"
#include "mirrortest/core/output.h"
#include "mirrortest/core/templates.h"
#include "mirrortest/ir/model.h"

namespace mt::backend {

struct GtestOptions {
    // `placeholder` (fail or skip) and `file_header`.
    core::Config config;

    // The header the plans were made from, as given on the command line; only
    // shown, in the header comment of every generated file.
    std::string source;

    // The directory the files go under. Empty = the working directory.
    std::string out_dir;
};

// One test file.
struct GtestViews {
    std::string    path;   // below the output directory: plan.file
    nlohmann::json view;   // for gtest/test_file.inja
};

// Pure: no rendering, no file system. Reports through `diagnostics`
// mt-unsupported for a suite with a fixture (they are generated from phase 6;
// until then its cases are written as plain TESTs).
std::vector<GtestViews> build_gtest_views(const std::vector<ir::TestPlan>& plans, const GtestOptions& options,
                                          core::Diagnostics& diagnostics);

// Views rendered through the template. A template that fails records an error
// and the file is left out of the plan; the caller refuses to write anything
// when `diagnostics` has errors.
core::OutputPlan generate_gtest(const std::vector<ir::TestPlan>& plans, const GtestOptions& options,
                                const core::TemplateStore& templates, core::Diagnostics& diagnostics);

}  // namespace mt::backend

#endif  // MT_BACKEND_GTEST_BACKEND_H
