// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The templates that lay generated files out (docs/design.md, section 15).
//
// A template only *places* text. Everything that needs a decision -- how a
// signature is spelt, which includes a type pulls in, what a stub's body is --
// is computed in C++ and handed over already formatted, in a JSON view model.
// That keeps the logic testable and the templates dull on purpose: a project
// that overrides one to match its house style cannot accidentally change what
// the tool infers.
//
// The defaults are compiled into the binary (templates/**/*.inja, see
// cmake/MtEmbedFiles.cmake). `--templates DIR` replaces any of them by a file
// of the same relative path; the rest stay embedded. Only embedded names can
// be overridden: a file in DIR that matches none is reported and ignored,
// since the usual cause is a typo and a silently ignored override is worse
// than an error.
//
// Rendering is inja with trim_blocks and lstrip_blocks, so block tags on a
// line of their own leave no blank line behind. A variable the view model
// does not define is an error, not an empty string. Templates may
// `{% include "other/name.inja" %}` any template in the store. Templates are
// LF; CRLF in an override is normalised.
//
// One inja quirk to know before writing a template: trim_blocks removes the
// spaces and tabs after a block tag as well as the following newline (Jinja
// removes only the newline). So `{% if x %} from` renders as "from" without
// its leading space. Keep the space inside an expression:
// `{% if x %}{{ " from " }}`. See docs/templates.md.

#ifndef MT_CORE_TEMPLATES_H
#define MT_CORE_TEMPLATES_H

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "mirrortest/core/diagnostics.h"

namespace mt::core {

// A template compiled into the binary. Both views point at static storage.
struct EmbeddedTemplate {
    std::string_view name;   // "impl/header.inja"
    std::string_view text;
};

// Everything under templates/ at build time, sorted by name.
std::vector<EmbeddedTemplate> embedded_templates();

class TemplateStore {
public:
    // `embedded` is the default set (embedded_templates() in production; tests
    // pass their own). Files under `override_dir` replace the entries of the
    // same relative path. Problems with the directory are recorded in
    // `diagnostics`: an unusable directory is an error, a file that matches no
    // template is a warning.
    TemplateStore(std::vector<EmbeddedTemplate> embedded,
                  const std::optional<std::filesystem::path>& override_dir,
                  Diagnostics& diagnostics);
    ~TemplateStore();
    TemplateStore(TemplateStore&&) noexcept;
    TemplateStore& operator=(TemplateStore&&) noexcept;
    TemplateStore(const TemplateStore&) = delete;
    TemplateStore& operator=(const TemplateStore&) = delete;

    bool has(std::string_view name) const;

    // Every template name, sorted.
    std::vector<std::string> names() const;

    // Which templates came from the override directory, sorted.
    std::vector<std::string> overridden() const;

    // Parses every template, recording one error for each that does not.
    // Rendering does this lazily; this is for checking up front.
    bool validate(Diagnostics& diagnostics) const;

    // Renders `name` with `data`. On any problem -- unknown template, a
    // template that does not parse, a variable `data` lacks -- records an
    // error that names the template and, where inja knows it, the line, and
    // returns nullopt.
    std::optional<std::string> render(std::string_view name, const nlohmann::json& data,
                                      Diagnostics& diagnostics) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mt::core

#endif  // MT_CORE_TEMPLATES_H
