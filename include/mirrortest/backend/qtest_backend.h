// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The Qt Test backend: framework-neutral test plans in, test files out
// (docs/design.md, section 9.5).
//
// The same two steps as the GoogleTest backend. A Qt test is a QObject whose
// private slots are the tests, so the file declares the class, defines each
// slot, and ends with the entry point and the include of the file moc writes:
//
//   class TestFoo : public QObject {
//       Q_OBJECT
//   private slots:
//       void size();
//   };
//
//   void TestFoo::size() { QFAIL("..."); ... }
//
//   QTEST_GUILESS_MAIN(TestFoo)
//   #include "tst_foo.moc"
//
// The `.moc` name follows the name of the file, which is what AUTOMOC needs; it
// is why the file is not free to be called anything. The view model and the
// template's variables are documented in docs/templates.md.

#ifndef MT_BACKEND_QTEST_BACKEND_H
#define MT_BACKEND_QTEST_BACKEND_H

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mirrortest/core/config.h"
#include "mirrortest/core/diagnostics.h"
#include "mirrortest/core/output.h"
#include "mirrortest/core/templates.h"
#include "mirrortest/ir/model.h"

namespace mt::backend {

struct QtestOptions {
    // `placeholder` (QFAIL or QSKIP) and `file_header`.
    core::Config config;

    // QTEST_APPLESS_MAIN, QTEST_GUILESS_MAIN or QTEST_MAIN.
    core::QtMain qt_main = core::QtMain::GuiLess;

    // The header the plans were made from, as given on the command line; only
    // shown, in the header comment of every generated file.
    std::string source;

    // The directory the files go under. Empty = the working directory.
    std::string out_dir;
};

// One test file.
struct QtestViews {
    std::string    path;   // below the output directory: plan.file
    nlohmann::json view;   // for qtest/test_file.inja
};

// Pure: no rendering, no file system. Reports through `diagnostics`
// mt-unsupported for a plan with a fixture (phase 6), and for a plan with
// several test classes, since a Qt Test executable has one entry point: it goes
// to the first class.
std::vector<QtestViews> build_qtest_views(const std::vector<ir::TestPlan>& plans, const QtestOptions& options,
                                          core::Diagnostics& diagnostics);

// Views rendered through the template. A template that fails records an error
// and the file is left out of the plan; the caller refuses to write anything
// when `diagnostics` has errors.
core::OutputPlan generate_qtest(const std::vector<ir::TestPlan>& plans, const QtestOptions& options,
                                const core::TemplateStore& templates, core::Diagnostics& diagnostics);

}  // namespace mt::backend

#endif  // MT_BACKEND_QTEST_BACKEND_H
