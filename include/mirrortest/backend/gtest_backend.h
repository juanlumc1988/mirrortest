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

// Pure: no rendering, no file system, and nothing to report: whatever a plan
// holds, GoogleTest can say it. A suite with a fixture is a fixture class and
// TEST_F tests; one without, TESTs.
std::vector<GtestViews> build_gtest_views(const std::vector<ir::TestPlan>& plans, const GtestOptions& options);

// One mock header.
struct GtestMockViews {
    std::string    path;   // below the output directory: mock.file
    nlohmann::json view;   // for gtest/mock_file.inja
};

// Pure. Every virtual function of a mock is a MOCK_METHOD(Ret, name, (Args), (specifiers));
// a type with a comma in it -- `std::map<int, int>` -- is put in parentheses, which is how
// gmock is told that the comma is not the macro's.
std::vector<GtestMockViews> build_gtest_mock_views(const std::vector<ir::MockClass>& mocks, const GtestOptions& options);

// Views rendered through gtest/mock_file.inja, with the same rules as generate_gtest.
core::OutputPlan generate_gtest_mocks(const std::vector<ir::MockClass>& mocks, const GtestOptions& options,
                                      const core::TemplateStore& templates, core::Diagnostics& diagnostics);

// Views rendered through the template. A template that fails records an error
// and the file is left out of the plan; the caller refuses to write anything
// when `diagnostics` has errors.
core::OutputPlan generate_gtest(const std::vector<ir::TestPlan>& plans, const GtestOptions& options,
                                const core::TemplateStore& templates, core::Diagnostics& diagnostics);

}  // namespace mt::backend

#endif  // MT_BACKEND_GTEST_BACKEND_H
