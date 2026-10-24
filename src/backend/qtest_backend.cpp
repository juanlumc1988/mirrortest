// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/backend/qtest_backend.h"

#include <string>
#include <utility>
#include <vector>

#include "mirrortest/backend/cxx_format.h"
#include "mirrortest/backend/test_render.h"
#include "mirrortest/ir/type.h"

namespace mt::backend {

namespace {

using json = nlohmann::json;

PlaceholderStyle placeholder_style(core::Placeholder placeholder) {
    // Both return from the slot: what follows is compiled, never run.
    return placeholder == core::Placeholder::Skip ? PlaceholderStyle{"QSKIP(\"", "\");"} : PlaceholderStyle{"QFAIL(\"", "\");"};
}

const char* main_macro(core::QtMain main) {
    switch (main) {
        case core::QtMain::Appless: return "QTEST_APPLESS_MAIN";
        case core::QtMain::GuiLess: return "QTEST_GUILESS_MAIN";
        case core::QtMain::Gui: return "QTEST_MAIN";
    }
    return "QTEST_GUILESS_MAIN";
}

// "tst_foo.moc" for "tst_foo.cpp" or "tests/tst_foo.cpp": the file name without its extension.
std::string moc_name(const std::string& file) {
    const std::size_t slash = file.find_last_of('/');
    std::string name = slash == std::string::npos ? file : file.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot != 0) name.erase(dot);
    return name + ".moc";
}

json class_view(const ir::TestSuite& suite, core::Placeholder placeholder) {
    json head = json::array();
    head.push_back("class " + suite.name + " : public QObject {");
    head.push_back("    Q_OBJECT");
    head.push_back("private slots:");
    for (const ir::TestCase& test : suite.cases) head.push_back("    void " + test.name + "();");
    head.push_back("};");

    json tests = json::array();
    for (const ir::TestCase& test : suite.cases) {
        json lines = json::array();
        lines.push_back("void " + suite.name + "::" + test.name + "() {");
        for (const ir::Stmt& s : test.body) lines.push_back(statement_line(s, placeholder_style(placeholder)));
        lines.push_back("}");

        json view;
        view["name"] = test.name;
        view["lines"] = lines;
        tests.push_back(std::move(view));
    }

    json view;
    view["name"] = suite.name;
    view["head"] = head;
    view["tests"] = tests;
    return view;
}

json view_of(const ir::TestPlan& plan, const QtestOptions& options, core::Diagnostics& diagnostics) {
    // One section per namespace, in the order the namespaces first appear.
    std::vector<std::vector<std::string>> namespaces;
    std::vector<json>                     classes_of;
    for (const ir::TestSuite& suite : plan.suites) {
        if (suite.fixture) {
            diagnostics.warning(core::code::unsupported,
                                "the fixture of '" + suite.name + "' is not generated yet (phase 6): its cases are written as plain slots");
        }
        std::size_t at = 0;
        while (at < namespaces.size() && namespaces[at] != suite.ns) ++at;
        if (at == namespaces.size()) {
            namespaces.push_back(suite.ns);
            classes_of.push_back(json::array());
        }
        classes_of[at].push_back(class_view(suite, options.config.placeholder));
    }
    if (plan.suites.size() > 1) {
        diagnostics.warning(core::code::unsupported,
                            "'" + plan.file + "' has " + std::to_string(plan.suites.size()) +
                                " test classes but a Qt Test program has one entry point: " + main_macro(options.qt_main) +
                                " names only '" + plan.suites[0].name + "'");
    }

    json sections = json::array();
    for (std::size_t i = 0; i < namespaces.size(); ++i) {
        json section;
        section["open"] = namespace_open(namespaces[i]);
        section["close"] = namespace_close(namespaces[i]);
        section["classes"] = classes_of[i];
        sections.push_back(std::move(section));
    }

    // The entry point and the include of moc's output come last, outside the namespaces.
    json main = json::array();
    if (!plan.suites.empty()) {
        std::string qualified;
        for (const std::string& n : plan.suites[0].ns) qualified += n + "::";
        qualified += plan.suites[0].name;
        main.push_back(std::string(main_macro(options.qt_main)) + "(" + qualified + ")");
        main.push_back("// AUTOMOC: the name of the moc file must follow the name of this file.");
        main.push_back("#include \"" + moc_name(plan.file) + "\"");
    }

    json view;
    view["file_header"] = options.config.file_header;
    view["source"] = options.source;
    view["includes"] = include_lines(plan.includes);
    view["sections"] = sections;
    view["main"] = main;
    return view;
}

}  // namespace

std::vector<QtestViews> build_qtest_views(const std::vector<ir::TestPlan>& plans, const QtestOptions& options,
                                          core::Diagnostics& diagnostics) {
    std::vector<QtestViews> out;
    for (const ir::TestPlan& plan : plans) out.push_back({plan.file, view_of(plan, options, diagnostics)});
    return out;
}

core::OutputPlan generate_qtest(const std::vector<ir::TestPlan>& plans, const QtestOptions& options,
                                const core::TemplateStore& templates, core::Diagnostics& diagnostics) {
    core::OutputPlan out;
    for (const QtestViews& v : build_qtest_views(plans, options, diagnostics)) {
        if (auto text = templates.render("qtest/test_file.inja", v.view, diagnostics)) {
            out.files.push_back({core::output_path(options.out_dir, v.path), *text});
        }
    }
    return out;
}

}  // namespace mt::backend
