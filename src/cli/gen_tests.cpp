// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// gen-tests: headers in, test skeletons out. The wiring of docs/design.md
// section 3: read the header, plan the tests, render the plan, write it.
// Everything with a decision in it lives in the modules this calls; what is
// left here is the order, how the generated test includes the header, and the
// refusal to write anything if a run recorded an error (D8).

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "mirrortest/backend/gtest_backend.h"
#include "mirrortest/backend/qtest_backend.h"
#include "mirrortest/backend/test_planner.h"
#include "mirrortest/cli/plan_collector.h"
#include "mirrortest/cli/run.h"
#include "mirrortest/core/include_search.h"
#include "mirrortest/core/source.h"
#include "mirrortest/core/templates.h"
#include "mirrortest/core/writer.h"
#include "mirrortest/frontend/code_frontend.h"

namespace mt::cli {

namespace {

namespace fs = std::filesystem;

// How the generated test includes the header (design 9.3): what --include-as
// says; else the path below the first -I directory that contains the header,
// which is how the code that uses it would write the include; else its name.
std::string include_path_for(const Options& options, const Env& env, const std::string& input) {
    if (options.include_as) return *options.include_as;

    // A path that cannot be resolved comes back empty, and path_below() finds an
    // empty path below nothing: the directory is skipped, not an error.
    std::error_code ec;
    const fs::path header = fs::weakly_canonical(resolve(env, input), ec);
    for (const std::string& dir : options.include_dirs) {
        if (const std::optional<std::string> below = core::path_below(header, fs::weakly_canonical(resolve(env, dir), ec))) {
            return *below;
        }
    }
    return fs::path(input).filename().generic_string();
}

std::vector<core::OutputFile> process_input(const Options& options, const Env& env, const core::TemplateStore& templates,
                                            const std::string& input, core::Diagnostics& diagnostics) {
    const std::optional<core::SourceFile> source = core::read_source(input, diagnostics, env.cwd);
    if (!source) return {};

    const core::Config& config = options.config;
    syntax::MacroTable macros;
    macros.ignore = config.ignore_macros;

    const std::string include = include_path_for(options, env, input);
    const ir::Model model = frontend::read_header(*source, include, macros, diagnostics);

    if (!model.functions.empty()) {
        core::Diagnostic note;
        note.severity = core::Severity::Note;
        note.code = core::code::unsupported;
        note.message = std::to_string(model.functions.size()) +
                       " free function(s) in this header get no tests: gen-tests covers classes (docs/design.md, section 9.1)";
        note.where = model.functions.front().where;
        diagnostics.add(std::move(note));
    }

    const ir::Framework framework = options.framework.value_or(ir::Framework::GTest);
    backend::PlannerOptions planner;
    planner.config = config;
    planner.framework = framework;
    planner.header_include = include;
    const std::vector<ir::TestPlan> plans = backend::plan_tests(model, planner, diagnostics);

    if (framework == ir::Framework::QTest) {
        backend::QtestOptions qtest;
        qtest.config = config;
        qtest.qt_main = options.qt_main;
        qtest.source = input;
        qtest.out_dir = options.out.value_or("");
        return backend::generate_qtest(plans, qtest, templates, diagnostics).files;
    }
    backend::GtestOptions gtest;
    gtest.config = config;
    gtest.source = input;
    gtest.out_dir = options.out.value_or("");
    return backend::generate_gtest(plans, gtest, templates, diagnostics).files;
}

}  // namespace

int run_gen_tests(const Options& options, const Env& env) {
    core::Diagnostics diagnostics;

    std::optional<fs::path> template_dir;
    if (options.templates_dir) template_dir = resolve(env, *options.templates_dir);
    const core::TemplateStore templates(core::embedded_templates(), template_dir, diagnostics);
    if (options.verbose) {
        for (const std::string& name : templates.overridden()) {
            *env.err << "mirrortest: using template '" << name << "' from '" << *options.templates_dir << "'\n";
        }
    }
    if (diagnostics.has_errors() || !templates.validate(diagnostics)) return finish_run(options, env, diagnostics, nullptr);

    PlanCollector collector;
    for (const std::string& input : options.inputs) {
        collector.add(input, process_input(options, env, templates, input, diagnostics),
                      "give the headers different output directories, or run them one at a time", diagnostics);
    }
    const core::OutputPlan plan = collector.take();

    if (options.strict) diagnostics.promote_warnings();
    if (diagnostics.has_errors()) return finish_run(options, env, diagnostics, nullptr);

    core::WriterOptions writer;
    writer.mode = options.overwrite;
    writer.dry_run = options.dry_run;
    writer.check = options.check;
    core::WriteReport report;
    core::write_plan(plan, env.cwd, writer, report, diagnostics);
    return finish_run(options, env, diagnostics, &report);
}

}  // namespace mt::cli
