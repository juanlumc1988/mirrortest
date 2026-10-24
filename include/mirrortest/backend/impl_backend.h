// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The implementation backend: the model inferred from a test in, the headers
// and stubs that make the test compile and fail out (docs/design.md, section 8).
//
// Two steps, so that each can be tested without the other:
//
//   build_impl_views   decides everything -- which class goes in which header,
//                      what each header includes, what every declaration and
//                      stub looks like -- and returns it as JSON view models.
//   generate_impl      renders those through the templates into an OutputPlan.
//
// Neither touches the file system (the writer does). The view models and the
// templates' variables are documented in docs/templates.md.

#ifndef MT_BACKEND_IMPL_BACKEND_H
#define MT_BACKEND_IMPL_BACKEND_H

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mirrortest/core/config.h"
#include "mirrortest/core/diagnostics.h"
#include "mirrortest/core/output.h"
#include "mirrortest/core/templates.h"
#include "mirrortest/ir/model.h"

namespace mt::backend {

struct ImplOptions {
    core::Config config;

    // The test file as the user gave it; only shown, in the header comment of
    // every generated file.
    std::string source;

    // The test's `#include "..."` that no file provides, as written and in
    // source order (design 8.1, 7.10). They are the headers to generate.
    std::vector<std::string> headers_to_generate;

    // Directories the files go under. Empty = the working directory. The
    // header lands at <out_include>/<path as included>, the source at
    // <out_src>/<the header's directory>/<naming.source>.
    std::string out_include;
    std::string out_src;

    // The headers the test includes that do exist, already read, each with its
    // `header` set to the include path the test uses for it. A generated
    // signature that mentions one of their types includes that header.
    std::vector<ir::Model> known;
};

// One header and, when it has anything to define, its source file.
struct ImplViews {
    std::string    header_path;    // as included: "acme/foo.h"
    nlohmann::json header_view;    // for impl/header.inja
    std::string    source_path;    // below the source directory: "acme/foo.cpp"; empty = no source file
    nlohmann::json source_view;    // for impl/source.inja; null when there is no source file
};

// Where a file lands: `dir` joined with the path below it, "" and "." meaning
// the working directory ("out" + "acme/foo.h" -> "out/acme/foo.h").
std::string output_path(const std::string& dir, const std::string& relative);

// Pure: no rendering, no file system. Reports through `diagnostics`:
// mt-no-include (a class or function no #include claims), mt-stub-may-pass
// (stub_policy "default"), mt-unsupported (a stub that cannot return an empty
// value, a path outside the output directory, region labels that collide).
std::vector<ImplViews> build_impl_views(const ir::Model& model, const ImplOptions& options, core::Diagnostics& diagnostics);

// Views rendered through the templates. A template that fails records an error
// and the file is left out of the plan; the caller refuses to write anything
// when `diagnostics` has errors.
core::OutputPlan generate_impl(const ir::Model& model, const ImplOptions& options, const core::TemplateStore& templates,
                               core::Diagnostics& diagnostics);

}  // namespace mt::backend

#endif  // MT_BACKEND_IMPL_BACKEND_H
