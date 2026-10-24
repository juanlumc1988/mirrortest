// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// gen-impl: test files in, the headers and stubs that make them compile (and
// fail) out. The wiring of docs/design.md section 3: read the test, find out
// which of its includes exist, infer the model, render it, write the plan.
// Everything with a decision in it lives in the modules this calls; what is
// left here is the order, and the refusal to write anything if a run recorded
// an error (D8).

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "mirrortest/backend/impl_backend.h"
#include "mirrortest/backend/impl_merge.h"
#include "mirrortest/cli/plan_collector.h"
#include "mirrortest/cli/run.h"
#include "mirrortest/core/include_search.h"
#include "mirrortest/core/source.h"
#include "mirrortest/core/templates.h"
#include "mirrortest/core/writer.h"
#include "mirrortest/frontend/code_frontend.h"
#include "mirrortest/frontend/facts.h"
#include "mirrortest/frontend/pipeline.h"
#include "mirrortest/frontend/test_frontend.h"

namespace mt::cli {

namespace {

namespace fs = std::filesystem;

core::SearchDir search_dir(const Env& env, const std::string& shown) {
    return {resolve(env, shown), shown};
}

// Which framework a test file is written for. A flag settles it; otherwise the
// file has to say, and say one thing.
std::optional<ir::Framework> choose_framework(const Options& options, const std::string& input,
                                              const frontend::TestFileFacts& facts, core::Diagnostics& diagnostics) {
    if (options.framework) return options.framework;
    const frontend::FrameworkEvidence seen = frontend::detect_framework(facts);
    if (seen.gtest && !seen.qtest) return ir::Framework::GTest;
    if (seen.qtest && !seen.gtest) return ir::Framework::QTest;
    ir::SourceLoc where;
    where.file = input;
    diagnostics.error(core::code::framework,
                      seen.gtest ? "the file uses both GoogleTest and QTest; say which with --framework"
                                 : "cannot tell which test framework the file uses: it includes neither <gtest/gtest.h> nor <QtTest>; "
                                   "say which with --framework",
                      where);
    return std::nullopt;
}

// What one test file contributes to the run: the plan its model renders to.
struct FileResult {
    core::OutputPlan plan;
};

FileResult process_input(const Options& options, const Env& env, const core::TemplateStore& templates, const std::string& input,
                         core::Diagnostics& diagnostics) {
    FileResult result;
    const std::optional<core::SourceFile> source = core::read_source(input, diagnostics, env.cwd);
    if (!source) return result;

    const core::Config& config = options.config;
    syntax::MacroTable macros;
    macros.ignore = config.ignore_macros;

    // A first reading, only for what the run needs before it can start: the
    // includes, and the framework. Its diagnostics are dropped; the analysis
    // below reads the file again and reports them once.
    core::Diagnostics scratch;
    frontend::TestFrontendOptions fo;
    fo.standard = config.cxx_std;
    fo.macros = macros;
    const frontend::TestFileFacts facts = frontend::read_test_file(*source, fo, scratch);

    const std::optional<ir::Framework> framework = choose_framework(options, input, facts, diagnostics);
    if (!framework) return result;
    // Which includes exist (their declarations are the API the test uses) and
    // which are ours to write.
    core::IncludeSearch search;
    const fs::path test_parent = fs::path(input).parent_path();
    search.test_dir = search_dir(env, test_parent.generic_string());
    for (const std::string& dir : options.include_dirs) search.include_dirs.push_back(search_dir(env, dir));
    search.out_include = search_dir(env, options.header_dir());

    std::vector<ir::Model> known;
    std::vector<std::string> to_generate;
    for (const core::ResolvedInclude& inc : core::resolve_includes(facts.includes, search)) {
        if (inc.kind == core::IncludeKind::Target) {
            to_generate.push_back(inc.path);
        } else if (inc.kind == core::IncludeKind::Known) {
            if (const std::optional<core::SourceFile> header = core::read_source(inc.shown, diagnostics, env.cwd)) {
                known.push_back(frontend::read_header(*header, inc.path, macros, diagnostics));
            }
        }
    }

    frontend::AnalysisOptions analysis_options;
    analysis_options.config = config;
    analysis_options.known = known;
    analysis_options.macros = macros;
    const frontend::Analysis analysis = frontend::analyse(*source, analysis_options, diagnostics);

    backend::ImplOptions impl;
    impl.config = config;
    impl.source = input;
    impl.headers_to_generate = std::move(to_generate);
    impl.out_include = options.header_dir();
    impl.out_src = options.source_dir();
    impl.known = std::move(known);
    result.plan = backend::generate_impl(analysis.model, impl, templates, diagnostics);
    return result;
}

}  // namespace

int run_gen_impl(const Options& options, const Env& env) {
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

    // One plan for all the test files. Two files that generate the same path
    // agree or the run stops: there is no merging of what two tests infer yet.
    PlanCollector collector;
    for (const std::string& input : options.inputs) {
        collector.add(input, process_input(options, env, templates, input, diagnostics).plan.files,
                      "generating one header from several test files is not supported yet, run them one at a time", diagnostics);
    }
    const core::OutputPlan plan = collector.take();

    if (options.strict) diagnostics.promote_warnings();
    if (diagnostics.has_errors()) return finish_run(options, env, diagnostics, nullptr);

    // --overwrite=merge: insert what is missing into the regions of the files that
    // are there. The merger reads the header a source goes with when that header was not merged.
    backend::ImplMerger merger(options.header_dir(), [&env](const std::string& path) -> std::optional<std::string> {
        std::ifstream in(resolve(env, path), std::ios::binary);
        if (!in) return std::nullopt;
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    });

    core::WriterOptions writer;
    writer.mode = options.overwrite;
    writer.dry_run = options.dry_run;
    writer.check = options.check;
    if (options.overwrite == core::OverwriteMode::Merge) writer.merge = merger.function();
    core::WriteReport report;
    core::write_plan(plan, env.cwd, writer, report, diagnostics);
    return finish_run(options, env, diagnostics, &report);
}

}  // namespace mt::cli
