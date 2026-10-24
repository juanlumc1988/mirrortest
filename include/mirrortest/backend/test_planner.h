// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The test planner: the model of a header in, framework-neutral test plans out
// (docs/design.md, section 9). What a test for a method must do is decided here
// once; the backend for each framework only spells it.
//
// A generated test is a skeleton that compiles against the real API and fails
// until somebody writes it (R19). Its first statement is the placeholder, which
// returns; everything after it is still compiled -- so the skeleton checks the
// API -- but never run, so a method of a legacy class is never called with
// zero-valued arguments on hardware. Where an argument cannot be built the call
// is kept as a comment, the best starting point for the person who writes it.

#ifndef MT_BACKEND_TEST_PLANNER_H
#define MT_BACKEND_TEST_PLANNER_H

#include <string>
#include <vector>

#include "mirrortest/core/config.h"
#include "mirrortest/core/diagnostics.h"
#include "mirrortest/ir/model.h"

namespace mt::backend {

struct PlannerOptions {
    core::Config  config;
    // Not consulted yet: the plan is the same for both frameworks until QTest
    // signals get their QSignalSpy skeleton (phase 5).
    ir::Framework framework = ir::Framework::GTest;

    // The header under test as the generated file includes it: "acme/foo.h".
    std::string header_include;
};

// One plan (one generated file) per class that has something public to test, in
// the order of the model. Reports through `diagnostics` what it leaves out:
// class templates (mt-unsupported, until instantiations are supported) and
// classes with nothing public (mt-unsupported note).
std::vector<ir::TestPlan> plan_tests(const ir::Model& model, const PlannerOptions& options, core::Diagnostics& diagnostics);

}  // namespace mt::backend

#endif  // MT_BACKEND_TEST_PLANNER_H
