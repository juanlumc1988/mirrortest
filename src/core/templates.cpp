// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/core/templates.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>

#include <inja/inja.hpp>

namespace mt::core {

namespace fs = std::filesystem;

namespace {

// Templates are LF whatever the checkout or the editor did to them.
std::string normalise_newlines(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

bool read_text(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

struct Source {
    std::string text;
    std::string shown;           // where to point the user in diagnostics
    bool        overridden = false;
};

Diagnostic template_error(const Source& source, const std::string& name, const std::string& message,
                          unsigned line = 0, unsigned column = 0) {
    Diagnostic d;
    d.severity = Severity::Error;
    d.code = code::config;
    d.message = "template '" + name + "': " + message;
    d.where.file = source.shown;
    d.where.line = line;
    d.where.column = column;
    return d;
}

}  // namespace

struct TemplateStore::Impl {
    std::map<std::string, Source> sources;   // sorted by name, which is the order we report in

    mutable inja::Environment                  env;
    mutable std::map<std::string, inja::Template> parsed;
    mutable std::vector<Diagnostic>            parse_errors;
    mutable bool                               parsed_all = false;

    Impl() {
        env.set_trim_blocks(true);
        env.set_lstrip_blocks(true);
        // Includes come from the store, never from whatever the current
        // directory happens to hold.
        env.set_search_included_templates_in_files(false);
        env.set_throw_at_missing_includes(true);
    }

    // Parses every template once and registers each as includable. Errors are
    // kept and replayed on every call, so a caller that asks twice is told
    // twice rather than failing silently the second time.
    bool parse_all(Diagnostics& diagnostics) const {
        if (!parsed_all) {
            parsed_all = true;
            for (const auto& entry : sources) {
                try {
                    inja::Template tmpl = env.parse(entry.second.text);
                    env.include_template(entry.first, tmpl);
                    parsed.emplace(entry.first, std::move(tmpl));
                } catch (const inja::InjaError& e) {
                    parse_errors.push_back(template_error(
                        entry.second, entry.first, e.message,
                        static_cast<unsigned>(e.location.line), static_cast<unsigned>(e.location.column)));
                } catch (const std::exception& e) {
                    parse_errors.push_back(template_error(entry.second, entry.first, e.what()));
                }
            }
        }
        for (const Diagnostic& d : parse_errors) {
            diagnostics.add(d);
        }
        return parse_errors.empty();
    }
};

TemplateStore::TemplateStore(std::vector<EmbeddedTemplate> embedded,
                             const std::optional<fs::path>& override_dir, Diagnostics& diagnostics)
    : impl_(std::make_unique<Impl>()) {
    for (const EmbeddedTemplate& t : embedded) {
        Source s;
        s.text = normalise_newlines(std::string(t.text));
        s.shown = "(built-in) " + std::string(t.name);
        impl_->sources[std::string(t.name)] = std::move(s);
    }
    if (!override_dir) {
        return;
    }

    const fs::path& dir = *override_dir;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        ir::SourceLoc where;
        where.file = dir.string();
        diagnostics.error(code::io, "the templates directory does not exist or is not a directory", where);
        return;
    }

    for (auto& entry : impl_->sources) {
        const fs::path file = dir / entry.first;
        if (!fs::is_regular_file(file, ec)) {
            continue;
        }
        std::string text;
        ir::SourceLoc where;
        where.file = file.string();
        if (!read_text(file, text)) {
            diagnostics.error(code::io, "cannot read the template", where);
            continue;
        }
        entry.second.text = normalise_newlines(std::move(text));
        entry.second.shown = file.string();
        entry.second.overridden = true;
    }

    // A template file that replaces nothing is almost certainly a misspelt
    // name, and the user would never see their change take effect.
    std::vector<std::string> strays;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".inja") {
            continue;
        }
        const std::string relative = fs::relative(it->path(), dir, ec).generic_string();
        if (impl_->sources.count(relative) == 0) {
            strays.push_back(relative);
        }
    }
    std::sort(strays.begin(), strays.end());   // directory order is unspecified
    for (const std::string& relative : strays) {
        ir::SourceLoc where;
        where.file = (dir / relative).string();
        diagnostics.warning(code::config,
                            "'" + relative + "' does not replace any template and is ignored", where);
    }
}

TemplateStore::~TemplateStore() = default;
TemplateStore::TemplateStore(TemplateStore&&) noexcept = default;
TemplateStore& TemplateStore::operator=(TemplateStore&&) noexcept = default;

bool TemplateStore::has(std::string_view name) const {
    return impl_->sources.count(std::string(name)) != 0;
}

std::vector<std::string> TemplateStore::names() const {
    std::vector<std::string> out;
    for (const auto& entry : impl_->sources) {
        out.push_back(entry.first);
    }
    return out;
}

std::vector<std::string> TemplateStore::overridden() const {
    std::vector<std::string> out;
    for (const auto& entry : impl_->sources) {
        if (entry.second.overridden) {
            out.push_back(entry.first);
        }
    }
    return out;
}

bool TemplateStore::validate(Diagnostics& diagnostics) const {
    return impl_->parse_all(diagnostics);
}

std::optional<std::string> TemplateStore::render(std::string_view name, const nlohmann::json& data,
                                                 Diagnostics& diagnostics) const {
    const std::string key(name);
    const auto source = impl_->sources.find(key);
    if (source == impl_->sources.end()) {
        diagnostics.error(code::config, "there is no template named '" + key + "'");
        return std::nullopt;
    }
    if (!impl_->parse_all(diagnostics)) {
        return std::nullopt;
    }

    try {
        return impl_->env.render(impl_->parsed.at(key), data);
    } catch (const inja::InjaError& e) {
        diagnostics.add(template_error(source->second, key, e.message,
                                       static_cast<unsigned>(e.location.line),
                                       static_cast<unsigned>(e.location.column)));
    } catch (const std::exception& e) {
        diagnostics.add(template_error(source->second, key, e.what()));
    }
    return std::nullopt;
}

}  // namespace mt::core
