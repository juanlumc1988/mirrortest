// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/backend/gtest_backend.h"

#include <string>
#include <utility>
#include <vector>

#include "mirrortest/backend/cxx_format.h"
#include "mirrortest/backend/test_render.h"
#include "mirrortest/core/naming.h"

namespace mt::backend {

namespace {

using json = nlohmann::json;

PlaceholderStyle placeholder_style(core::Placeholder placeholder) {
    // Both return from the test: what follows is compiled, never run.
    return placeholder == core::Placeholder::Skip ? PlaceholderStyle{"GTEST_SKIP() << \"", "\";"}
                                                  : PlaceholderStyle{"FAIL() << \"", "\";"};
}

// One line deeper: the statements of a method of the fixture sit inside the class.
std::string deeper(const std::string& line) { return line.empty() ? line : "    " + line; }

// The fixture as the class a TEST_F runs in. Each part is there only if it has something to say.
json fixture_view(const ir::Fixture& fixture, core::Placeholder placeholder) {
    const PlaceholderStyle style = placeholder_style(placeholder);
    json lines = json::array();
    lines.push_back("class " + fixture.name + " : public ::testing::Test {");
    lines.push_back("protected:");

    bool separate = false;
    const auto section = [&](const std::string& head, const std::vector<ir::Stmt>& body) {
        if (separate) lines.push_back("");
        lines.push_back("    " + head + " {");
        for (const ir::Stmt& s : body) lines.push_back(deeper(statement_line(s, style)));
        lines.push_back("    }");
        separate = true;
    };
    if (!fixture.initialisers.empty()) {
        std::string list;
        for (const std::string& init : fixture.initialisers) list += (list.empty() ? "" : ", ") + init;
        lines.push_back("    " + fixture.name + "() : " + list + " {}");
        separate = true;
    }
    if (!fixture.set_up.empty()) section("void SetUp() override", fixture.set_up);
    if (!fixture.tear_down.empty()) section("void TearDown() override", fixture.tear_down);
    if (separate && !fixture.members.empty()) lines.push_back("");
    for (const ir::Stmt& s : fixture.members) lines.push_back(statement_line(s, style));
    lines.push_back("};");

    json view;
    view["name"] = fixture.name;
    view["lines"] = lines;
    return view;
}

json test_view(const ir::TestSuite& suite, const ir::TestCase& test, core::Placeholder placeholder) {
    json lines = json::array();
    lines.push_back(std::string(suite.fixture ? "TEST_F(" : "TEST(") + suite.name + ", " + test.name + ") {");
    for (const ir::Stmt& s : test.body) lines.push_back(statement_line(s, placeholder_style(placeholder)));
    lines.push_back("}");

    json view;
    view["suite"] = suite.name;
    view["name"] = test.name;
    view["lines"] = lines;
    return view;
}

json view_of(const ir::TestPlan& plan, const GtestOptions& options) {
    // One section per namespace, in the order the namespaces first appear.
    std::vector<std::vector<std::string>> namespaces;
    std::vector<json>                     fixtures_of;
    std::vector<json>                     tests_of;
    for (const ir::TestSuite& suite : plan.suites) {
        std::size_t at = 0;
        while (at < namespaces.size() && namespaces[at] != suite.ns) ++at;
        if (at == namespaces.size()) {
            namespaces.push_back(suite.ns);
            fixtures_of.push_back(json::array());
            tests_of.push_back(json::array());
        }
        if (suite.fixture) fixtures_of[at].push_back(fixture_view(*suite.fixture, options.config.placeholder));
        for (const ir::TestCase& test : suite.cases) tests_of[at].push_back(test_view(suite, test, options.config.placeholder));
    }

    json sections = json::array();
    for (std::size_t i = 0; i < namespaces.size(); ++i) {
        json section;
        section["open"] = namespace_open(namespaces[i]);
        section["close"] = namespace_close(namespaces[i]);
        section["fixtures"] = fixtures_of[i];
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

// A type as a macro argument: the preprocessor splits the arguments at every comma that is not
// in parentheses, so `std::map<int, int>` is told apart as `(std::map<int, int>)`.
std::string macro_type(const ir::Type& type) {
    return type.spelling.find(',') == std::string::npos ? type.spelling : "(" + type.spelling + ")";
}

std::string mock_method_line(const ir::MockMethod& m) {
    std::string args;
    for (const ir::Type& p : m.params) args += (args.empty() ? "" : ", ") + macro_type(p);

    std::string specs;
    const auto add = [&specs](const std::string& spec) { specs += (specs.empty() ? "" : ", ") + spec; };
    if (m.is_const) add("const");
    if (m.ref_qualifier == ir::RefKind::LValue) add("ref(&)");
    if (m.ref_qualifier == ir::RefKind::RValue) add("ref(&&)");
    if (m.is_noexcept) add("noexcept");
    add("override");
    return "    MOCK_METHOD(" + macro_type(m.ret) + ", " + m.name + ", (" + args + "), (" + specs + "));";
}

json mock_view(const ir::MockClass& mock, const GtestOptions& options) {
    json lines = json::array();
    for (const std::string& note : mock.notes) lines.push_back(todo_comment(note));
    lines.push_back("class " + mock.name + " : public " + mock.base + " {");
    lines.push_back("public:");
    for (const ir::MockMethod& m : mock.methods) lines.push_back(mock_method_line(m));
    lines.push_back("};");

    std::vector<ir::Include> includes;
    includes.push_back({"gmock/gmock.h", true, {}});
    includes.push_back({mock.header, false, {}});

    json view;
    view["file_header"] = options.config.file_header;
    view["source"] = options.source;
    view["pragma_once"] = options.config.guard == core::GuardStyle::PragmaOnce;
    view["guard"] = core::include_guard(mock.file);
    view["includes"] = include_lines(includes);
    view["open"] = namespace_open(mock.ns);
    view["close"] = namespace_close(mock.ns);
    view["lines"] = lines;
    return view;
}

}  // namespace

std::vector<GtestMockViews> build_gtest_mock_views(const std::vector<ir::MockClass>& mocks, const GtestOptions& options) {
    std::vector<GtestMockViews> out;
    for (const ir::MockClass& mock : mocks) out.push_back({mock.file, mock_view(mock, options)});
    return out;
}

core::OutputPlan generate_gtest_mocks(const std::vector<ir::MockClass>& mocks, const GtestOptions& options,
                                      const core::TemplateStore& templates, core::Diagnostics& diagnostics) {
    core::OutputPlan out;
    for (const GtestMockViews& v : build_gtest_mock_views(mocks, options)) {
        if (auto text = templates.render("gtest/mock_file.inja", v.view, diagnostics)) {
            out.files.push_back({core::output_path(options.out_dir, v.path), *text});
        }
    }
    return out;
}

std::vector<GtestViews> build_gtest_views(const std::vector<ir::TestPlan>& plans, const GtestOptions& options) {
    std::vector<GtestViews> out;
    for (const ir::TestPlan& plan : plans) out.push_back({plan.file, view_of(plan, options)});
    return out;
}

core::OutputPlan generate_gtest(const std::vector<ir::TestPlan>& plans, const GtestOptions& options,
                                const core::TemplateStore& templates, core::Diagnostics& diagnostics) {
    core::OutputPlan out;
    for (const GtestViews& v : build_gtest_views(plans, options)) {
        if (auto text = templates.render("gtest/test_file.inja", v.view, diagnostics)) {
            out.files.push_back({core::output_path(options.out_dir, v.path), *text});
        }
    }
    return out;
}

}  // namespace mt::backend
