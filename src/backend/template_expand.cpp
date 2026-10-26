// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/backend/template_expand.h"

#include <cctype>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "mirrortest/ir/type.h"

namespace mt::backend {

namespace {

bool is_name_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool is_name_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

std::string trim(const std::string& s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])) != 0) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) --end;
    return s.substr(begin, end - begin);
}

// Splits `text` at the commas that are not inside <>, (), [] or {}.
std::vector<std::string> split_top_level(const std::string& text) {
    std::vector<std::string> out;
    int depth = 0;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '<' || c == '(' || c == '[' || c == '{') ++depth;
        else if (c == '>' || c == ')' || c == ']' || c == '}') --depth;
        else if (c == ',' && depth == 0) {
            out.push_back(trim(text.substr(begin, i - begin)));
            begin = i + 1;
        }
    }
    out.push_back(trim(text.substr(begin)));
    return out;
}

// The index of the `=` that starts a default, not one inside brackets (`N = (a == b)`) and not
// part of `==`, `<=`, `>=`, `!=`; npos if there is none.
std::size_t default_position(const std::string& part) {
    int depth = 0;
    for (std::size_t i = 0; i < part.size(); ++i) {
        const char c = part[i];
        if (c == '<' || c == '(' || c == '[' || c == '{') ++depth;
        else if (c == '>' || c == ')' || c == ']' || c == '}') --depth;
        else if (c == '=' && depth == 0) {
            const char before = i > 0 ? part[i - 1] : ' ';
            const char after = i + 1 < part.size() ? part[i + 1] : ' ';
            if (after != '=' && before != '=' && before != '!' && before != '<' && before != '>') return i;
        }
    }
    return std::string::npos;
}

// The last identifier of a parameter's declaration: `typename T` -> "T", `int N` -> "N",
// `template <typename> class C` -> "C", `typename` -> "".
std::string parameter_name(const std::string& declaration) {
    std::size_t end = declaration.size();
    while (end > 0 && !is_name_char(declaration[end - 1])) --end;
    std::size_t begin = end;
    while (begin > 0 && is_name_char(declaration[begin - 1])) --begin;
    const std::string word = declaration.substr(begin, end - begin);
    // The keyword of an unnamed parameter is not its name.
    if (word == "typename" || word == "class" || word == "struct" || word == "int" || word == "unsigned" || word == "bool" ||
        word == "char" || word == "long" || word == "short" || word == "size_t") {
        return {};
    }
    return word;
}

// `text` with the template parameters replaced by their arguments, and the class's own name
// (the injected-class-name, `Box` where `Box<T>` is meant) by the instantiation's. A name after
// `::` or `.` belongs to something else, and a number is not a name.
std::string substitute(const std::string& text, const std::map<std::string, std::string>& with, const std::string& own_name,
                       const std::string& instance) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            while (i < text.size() && (is_name_char(text[i]) || text[i] == '.' || text[i] == '\'')) out += text[i++];
            continue;
        }
        if (!is_name_start(c)) {
            out += c;
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < text.size() && is_name_char(text[end])) ++end;
        const std::string word = text.substr(i, end - i);
        const bool scoped = !out.empty() && (out.back() == ':' || out.back() == '.');
        std::size_t next = end;
        while (next < text.size() && text[next] == ' ') ++next;

        if (!scoped && word == own_name && !(next < text.size() && text[next] == '<')) {
            out += instance;
        } else if (!scoped && with.count(word) != 0) {
            out += with.at(word);
        } else {
            out += word;
        }
        i = end;
    }
    return out;
}

class Instantiator {
public:
    Instantiator(const std::map<std::string, std::string>& with, std::string own_name, std::string instance)
        : with_(with), own_name_(std::move(own_name)), instance_(std::move(instance)) {}

    std::string text(const std::string& s) const { return substitute(s, with_, own_name_, instance_); }

    ir::Type type(const ir::Type& t) const {
        if (t.empty()) return t;
        const std::string spelling = text(t.spelling);
        if (spelling == t.spelling) return t;
        ir::Type out = ir::type_from_spelling(spelling);
        out.origin = t.origin;
        out.reason = t.reason;
        out.where = t.where;
        return out;
    }

    ir::Method method(ir::Method m) const {
        m.ret = type(m.ret);
        for (ir::Param& p : m.params) {
            p.type = type(p.type);
            if (p.default_value) p.default_value = text(*p.default_value);
        }
        return m;
    }

private:
    const std::map<std::string, std::string>& with_;
    std::string                               own_name_;
    std::string                               instance_;
};

}  // namespace

bool parse_template_params(const std::string& text, std::vector<TemplateParam>& out) {
    out.clear();
    if (trim(text).empty()) return true;
    for (const std::string& part : split_top_level(text)) {
        TemplateParam p;
        const std::size_t eq = default_position(part);
        const std::string declaration = trim(eq == std::string::npos ? part : part.substr(0, eq));
        if (declaration.find("...") != std::string::npos) return false;
        if (eq != std::string::npos) {
            p.has_default = true;
            p.default_value = trim(part.substr(eq + 1));
        }
        p.name = parameter_name(declaration);
        out.push_back(std::move(p));
    }
    return true;
}

std::vector<std::string> split_template_arguments(const std::string& text) {
    if (trim(text).empty()) return {};
    return split_top_level(text);
}

ir::Model instantiate_templates(const ir::Model& model, const core::Config& config, core::Diagnostics& diagnostics) {
    ir::Model out = model;
    out.classes.clear();
    for (const ir::Class& c : model.classes) {
        const std::string qualified = ir::qualified_name(c.ns, c.name);
        const auto wanted = config.template_instantiations.find(qualified);
        if (!c.template_params || wanted == config.template_instantiations.end()) {
            out.classes.push_back(c);
            continue;
        }

        std::vector<TemplateParam> params;
        if (!parse_template_params(*c.template_params, params)) {
            diagnostics.warning(core::code::config,
                                "template_instantiations: '" + qualified + "' has a parameter pack, which a test cannot instantiate; "
                                "the class is not tested",
                                c.where);
            out.classes.push_back(c);   // kept as the template: the planner says it is skipped
            continue;
        }

        bool any = false;
        for (const std::string& list : wanted->second) {
            const std::vector<std::string> args = split_template_arguments(list);
            const std::string shown = qualified + "<" + list + ">";
            if (args.size() > params.size()) {
                diagnostics.warning(core::code::config,
                                    "template_instantiations: '" + shown + "' gives " + std::to_string(args.size()) +
                                        " arguments and the template has " + std::to_string(params.size()),
                                    c.where);
                continue;
            }

            // Each parameter's argument: the one given, else its default with the earlier parameters in it.
            std::map<std::string, std::string> with;
            std::vector<std::string> values;
            bool complete = true;
            for (std::size_t i = 0; i < params.size() && complete; ++i) {
                std::string value;
                if (i < args.size()) {
                    value = args[i];
                } else if (params[i].has_default) {
                    value = substitute(params[i].default_value, with, std::string(), std::string());
                } else {
                    diagnostics.warning(core::code::config,
                                        "template_instantiations: '" + shown + "' gives " + std::to_string(args.size()) +
                                            " arguments and parameter " + std::to_string(i + 1) + " of the template has no default",
                                        c.where);
                    complete = false;
                    break;
                }
                value = ir::normalize_spelling(value);
                values.push_back(value);
                if (!params[i].name.empty()) with[params[i].name] = value;
            }
            if (!complete) continue;

            std::string joined;
            for (std::size_t i = 0; i < args.size(); ++i) joined += (i == 0 ? "" : ", ") + ir::normalize_spelling(args[i]);
            const std::string instance = c.name + "<" + joined + ">";

            const Instantiator inst(with, c.name, instance);
            ir::Class made = c;
            made.name = instance;
            made.template_params.reset();
            for (ir::Method& m : made.methods) m = inst.method(m);
            for (ir::Field& f : made.fields) f.type = inst.type(f.type);
            for (ir::BaseSpec& b : made.bases) b.name = inst.text(b.name);
            out.classes.push_back(std::move(made));
            any = true;
        }
        if (!any) out.classes.push_back(c);   // nothing could be made: the planner says the template is skipped
    }
    return out;
}

}  // namespace mt::backend
