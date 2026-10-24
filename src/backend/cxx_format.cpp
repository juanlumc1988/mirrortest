// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/backend/cxx_format.h"

#include <cstddef>

#include "mirrortest/ir/type.h"

namespace mt::backend {

namespace {

// Where in a type spelling the declared name goes, if not at the end.
// `void (*)(int)` -> after "(*", `int[3]` -> before "[".
std::size_t name_position(const std::string& spelling) {
    const std::size_t ptr = spelling.find("(*)");
    if (ptr != std::string::npos) return ptr + 2;
    const std::size_t ref = spelling.find("(&)");
    if (ref != std::string::npos) return ref + 2;
    const std::size_t bracket = spelling.find('[');
    if (bracket != std::string::npos && spelling.back() == ']') return bracket;
    return std::string::npos;
}

std::string params_text(const std::vector<ir::Param>& params, bool keep_names, bool with_defaults) {
    std::string out;
    for (std::size_t i = 0; i < params.size(); ++i) {
        const ir::Param& p = params[i];
        if (i != 0) out += ", ";
        if (keep_names) {
            out += declare(p.type, p.name);
        } else {
            // A commented name keeps the text readable (`int /*count*/`) without
            // declaring a variable nobody uses.
            out += p.type.spelling;
            if (!p.name.empty()) out += " /*" + p.name + "*/";
        }
        if (with_defaults && p.default_value) out += " = " + *p.default_value;
    }
    return out;
}

std::string qualifiers(const ir::Method& m) {
    std::string out;
    if (m.is_const) out += " const";
    if (m.ref_qualifier == ir::RefKind::LValue) out += " &";
    if (m.ref_qualifier == ir::RefKind::RValue) out += " &&";
    if (m.is_noexcept) out += " noexcept";
    return out;
}

bool has_return_type(const ir::Method& m) {
    return m.kind != ir::MethodKind::Constructor && m.kind != ir::MethodKind::Destructor && m.kind != ir::MethodKind::Conversion;
}

std::string escape_literal(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::string types_text(const std::vector<ir::Param>& params) {
    std::string out;
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i != 0) out += ", ";
        out += params[i].type.spelling;
    }
    return out;
}

}  // namespace

std::string declare(const ir::Type& type, const std::string& name) {
    if (name.empty()) return type.spelling;
    const std::size_t at = name_position(type.spelling);
    if (at == std::string::npos) return type.spelling + " " + name;
    // The spelling is normalised, so `void (*)(int)` is `void(*)(int)` here and
    // the name slots in without a space; before the brackets of an array it
    // needs one.
    const char* gap = type.spelling[at] == '[' ? " " : "";
    return type.spelling.substr(0, at) + gap + name + type.spelling.substr(at);
}

std::string method_declaration(const ir::Method& m) {
    std::string out;
    if (m.is_virtual) out += "virtual ";
    if (m.is_static) out += "static ";
    if (m.is_explicit) out += "explicit ";
    if (has_return_type(m)) out += m.ret.spelling + " ";
    out += m.name + "(" + params_text(m.params, true, true) + ")" + qualifiers(m);
    if (m.is_override) out += " override";
    if (m.is_final) out += " final";
    if (m.is_pure) out += " = 0";
    else if (m.is_deleted) out += " = delete";
    else if (m.is_defaulted) out += " = default";
    return out + ";";
}

std::string method_definition_head(const ir::Class& cls, const ir::Method& m) {
    std::string out;
    if (has_return_type(m)) out += m.ret.spelling + " ";
    // The qualifiers are part of the function's type, noexcept included: the
    // definition has to repeat them or it defines a different function.
    out += cls.name + "::" + m.name + "(" + params_text(m.params, false, false) + ")" + qualifiers(m);
    return out;
}

bool needs_definition(const ir::Method& m) { return !m.is_pure && !m.is_deleted && !m.is_defaulted; }

std::string field_declaration(const ir::Field& f) { return std::string(f.is_static ? "static " : "") + declare(f.type, f.name) + ";"; }

std::string field_definition(const ir::Class& cls, const ir::Field& f) {
    if (!f.is_static) return {};
    // Value-initialised, so the definition is valid for any type that can be
    // and, for the fundamental ones, is zero rather than indeterminate.
    return declare(f.type, cls.name + "::" + f.name) + "{};";
}

std::string function_declaration(const ir::Function& f) {
    return f.ret.spelling + " " + f.name + "(" + params_text(f.params, true, true) + ")" + (f.is_noexcept ? " noexcept" : "") + ";";
}

std::string function_definition_head(const ir::Function& f) {
    return f.ret.spelling + " " + f.name + "(" + params_text(f.params, false, false) + ")" + (f.is_noexcept ? " noexcept" : "");
}

std::string method_signature(const ir::Class& cls, const ir::Method& m) {
    std::string out = ir::qualified_name(cls.ns, cls.name) + "::" + m.name + "(" + types_text(m.params) + ")";
    if (m.is_const) out += " const";
    if (m.ref_qualifier == ir::RefKind::LValue) out += " &";
    if (m.ref_qualifier == ir::RefKind::RValue) out += " &&";
    return out;   // noexcept is left out: it does not tell overloads apart
}

std::string function_signature(const ir::Function& f) {
    return ir::qualified_name(f.ns, f.name) + "(" + types_text(f.params) + ")";
}

std::string todo_comment(const std::string& note) { return "// TODO(mt): " + note; }

Stub make_stub(core::StubPolicy policy, const ir::Type& ret, bool trivial, bool default_returnable, const std::string& signature) {
    Stub stub;
    if (trivial) return stub;
    switch (policy) {
        case core::StubPolicy::Throw:
            stub.body.push_back("throw std::logic_error(\"mirrortest: not implemented: " + escape_literal(signature) + "\");");
            stub.include = "stdexcept";
            break;
        case core::StubPolicy::Abort:
            stub.body.push_back("std::abort();");
            stub.include = "cstdlib";
            break;
        case core::StubPolicy::Default:
            if (ret.is_void()) break;
            if (default_returnable && ret.ref == ir::RefKind::None) {
                stub.body.push_back("return {};");
            } else {
                stub.body.push_back("std::abort();");
                stub.include = "cstdlib";
                stub.fell_back = true;
            }
            break;
    }
    return stub;
}

}  // namespace mt::backend
