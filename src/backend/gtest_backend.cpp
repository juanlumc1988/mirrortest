// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/backend/gtest_backend.h"

#include <string>
#include <utility>
#include <vector>

#include "mirrortest/backend/cxx_format.h"
#include "mirrortest/backend/test_render.h"

namespace mt::backend {

namespace {

using json = nlohmann::json;

PlaceholderStyle placeholder_style(core::Placeholder placeholder) {
    // Both return from the test: what follows is compiled, never run.
    return placeholder == core::Placeholder::Skip ? PlaceholderStyle{"GTEST_SKIP() << \"", "\";"}
                                                  : PlaceholderStyle{"FAIL() << \"", "\";"};
}

json test_view(const ir::TestSuite& suite, const ir::TestCase& test, core::Placeholder placeholder) {
    json lines = json::array();
    lines.push_back("TEST(" + suite.name + ", " + test.name + ") {");
    for (const ir::Stmt& s : test.body) lines.push_back(statement_line(s, placeholder_style(placeholder)));
    lines.push_back("}");

    json view;
    view["suite"] = suite.name;
    view["name"] = test.name;
    view["lines"] = lines;
    return view;
}

json view_of(const ir::TestPlan& plan, const GtestOptions& options, core::Diagnostics& diagnostics) {
    // One section per namespace, in the order the namespaces first appear.
    std::vector<std::vector<std::string>> namespaces;
    std::vector<json>                     tests_of;
    for (const ir::TestSuite& suite : plan.suites) {
        if (suite.fixture) {
            diagnostics.warning(core::code::unsupported,
                                "the fixture of '" + suite.name + "' is not generated yet (phase 6): its cases are written as plain TESTs");
        }
        std::size_t at = 0;
        while (at < namespaces.size() && namespaces[at] != suite.ns) ++at;
        if (at == namespaces.size()) {
            namespaces.push_back(suite.ns);
            tests_of.push_back(json::array());
        }
        for (const ir::TestCase& test : suite.cases) tests_of[at].push_back(test_view(suite, test, options.config.placeholder));
    }

    json sections = json::array();
    for (std::size_t i = 0; i < namespaces.size(); ++i) {
        json section;
        section["open"] = namespace_open(namespaces[i]);
        section["close"] = namespace_close(namespaces[i]);
        section["tests"] = tests_of[i];
        sections.push_back(std::move(section));
    }

    json view;
    view["file_header"] = options.config.file_header;
    view["source"] = options.source;
    view["includes"] = include_lines(plan.includes);
    view["sections"] = sections;
    return view;
}

}  // namespace

std::vector<GtestViews> build_gtest_views(const std::vector<ir::TestPlan>& plans, const GtestOptions& options,
                                          core::Diagnostics& diagnostics) {
    std::vector<GtestViews> out;
    for (const ir::TestPlan& plan : plans) out.push_back({plan.file, view_of(plan, options, diagnostics)});
    return out;
}

core::OutputPlan generate_gtest(const std::vector<ir::TestPlan>& plans, const GtestOptions& options,
                                const core::TemplateStore& templates, core::Diagnostics& diagnostics) {
    core::OutputPlan out;
    for (const GtestViews& v : build_gtest_views(plans, options, diagnostics)) {
        if (auto text = templates.render("gtest/test_file.inja", v.view, diagnostics)) {
            out.files.push_back({core::output_path(options.out_dir, v.path), *text});
        }
    }
    return out;
}

}  // namespace mt::backend
