// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// A model as text, every field of the code half of it, in a fixed order: for tests that need to say "these two
// readers made the same model" (the tree-sitter and the libclang frontends) and see where they did not.

#ifndef MT_TESTS_MODEL_DUMP_H
#define MT_TESTS_MODEL_DUMP_H

#include <sstream>
#include <string>
#include <vector>

#include "mirrortest/ir/model.h"

namespace mt::test {

inline const char* name_of(mt::ir::Access a) {
    return a == mt::ir::Access::Public ? "public" : a == mt::ir::Access::Protected ? "protected" : "private";
}

inline std::string dump_type(const mt::ir::Type& t) {
    std::ostringstream out;
    out << '"' << t.spelling << "\" base=\"" << t.base << "\" const=" << t.is_const << " ref=" << static_cast<int>(t.ref)
        << " ptr=" << t.pointer_depth;
    return out.str();
}

inline std::string dump_where(const mt::ir::SourceLoc& w) {
    return std::to_string(w.line) + ":" + std::to_string(w.column);
}

// The conditions of the #if branches a declaration is under: [defined(A)][!(defined(A))].
inline std::string join_guards(const std::vector<std::string>& guards) {
    std::string out;
    for (const std::string& g : guards) out += "[" + g + "]";
    return out;
}

inline void dump_class(std::ostringstream& out, const mt::ir::Class& c, const std::string& indent, bool with_positions) {
    out << indent << (c.is_struct ? "struct " : "class ") << c.name;
    for (const std::string& n : c.ns) out << " ns:" << n;
    if (c.template_params) out << " template<" << *c.template_params << ">";
    out << " header=" << c.header << " qobject=" << c.is_qobject << " gadget=" << c.is_gadget << " nocopy=" << c.disable_copy
        << " nested_access=" << name_of(c.nested_access);
    if (!c.guards.empty()) out << " guards=" << join_guards(c.guards);
    if (with_positions) out << " @" << dump_where(c.where);
    out << "\n";
    for (const mt::ir::BaseSpec& b : c.bases) {
        out << indent << "  base " << name_of(b.access) << (b.is_virtual ? " virtual " : " ") << b.name << "\n";
    }
    for (const std::string& n : c.nested_types) out << indent << "  nested-type " << n << "\n";
    for (const std::string& n : c.nested_enums) out << indent << "  nested-enum " << n << "\n";
    for (const mt::ir::Method& m : c.methods) {
        out << indent << "  method " << name_of(m.access) << " kind=" << static_cast<int>(m.kind) << " " << m.name << " ret=" << dump_type(m.ret)
            << " flags=" << (m.is_const ? "const," : "") << (m.is_static ? "static," : "") << (m.is_virtual ? "virtual," : "")
            << (m.is_pure ? "pure," : "") << (m.is_override ? "override," : "") << (m.is_final ? "final," : "")
            << (m.is_noexcept ? "noexcept," : "") << (m.is_explicit ? "explicit," : "") << (m.is_deleted ? "deleted," : "")
            << (m.is_defaulted ? "defaulted," : "") << (m.is_template ? "template," : "") << " ref=" << static_cast<int>(m.ref_qualifier)
            << " qt=" << static_cast<int>(m.qt_role);
        if (!m.guards.empty()) out << " guards=" << join_guards(m.guards);
        if (with_positions) out << " @" << dump_where(m.where);
        out << "\n";
        for (const mt::ir::Param& p : m.params) {
            out << indent << "    param " << dump_type(p.type) << " name=\"" << p.name << "\"";
            if (p.default_value) out << " default=\"" << *p.default_value << "\"";
            out << "\n";
        }
    }
    for (const mt::ir::Field& f : c.fields) {
        out << indent << "  field " << name_of(f.access) << (f.is_static ? " static " : " ") << f.name << " " << dump_type(f.type);
        if (with_positions) out << " @" << dump_where(f.where);
        out << "\n";
    }
    for (const mt::ir::Class& n : c.nested_classes) dump_class(out, n, indent + "  ", with_positions);
}

inline std::string dump(const mt::ir::Model& model, bool with_positions = false) {
    std::ostringstream out;
    for (const mt::ir::Include& i : model.includes) {
        out << "include " << (i.is_system ? "<" : "\"") << i.path << (i.is_system ? ">" : "\"");
        if (with_positions) out << " @" << dump_where(i.where);
        out << "\n";
    }
    for (const mt::ir::Enum& e : model.enums) {
        out << (e.is_scoped ? "enum class " : "enum ") << e.name;
        for (const std::string& n : e.ns) out << " ns:" << n;
        if (e.underlying) out << " : " << *e.underlying;
        out << " {";
        for (const std::string& v : e.enumerators) out << " " << v;
        out << " } header=" << e.header;
        if (with_positions) out << " @" << dump_where(e.where);
        out << "\n";
    }
    for (const mt::ir::Function& f : model.functions) {
        out << "function " << f.name;
        for (const std::string& n : f.ns) out << " ns:" << n;
        out << " ret=" << dump_type(f.ret) << " noexcept=" << f.is_noexcept << " header=" << f.header;
        if (with_positions) out << " @" << dump_where(f.where);
        out << "\n";
        for (const mt::ir::Param& p : f.params) {
            out << "    param " << dump_type(p.type) << " name=\"" << p.name << "\"";
            if (p.default_value) out << " default=\"" << *p.default_value << "\"";
            out << "\n";
        }
    }
    for (const mt::ir::Class& c : model.classes) dump_class(out, c, "", with_positions);
    return out.str();
}

}  // namespace mt::test

#endif  // MT_TESTS_MODEL_DUMP_H
