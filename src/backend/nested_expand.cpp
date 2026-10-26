// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/backend/nested_expand.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "mirrortest/backend/cxx_format.h"
#include "mirrortest/ir/type.h"

namespace mt::backend {

namespace {

// A class around the one being flattened: its path from the namespace, and the types it declares.
struct Around {
    std::string              path;
    std::vector<std::string> nested_types;
};

// `spelling` as written in the nested class, written for outside the class: the types of every
// class around it get their path, the nearest class first (a name it declares hides the same name
// further out, and once qualified it is left alone), except the ones the nested class declares
// itself, which the planner qualifies with its own name.
std::string outside(const std::string& spelling, const std::vector<Around>& around, const ir::Class& nested) {
    std::string out = spelling;
    for (auto it = around.rbegin(); it != around.rend(); ++it) {
        ir::Class scope;
        scope.name = it->path;
        for (const std::string& type : it->nested_types) {
            if (std::find(nested.nested_types.begin(), nested.nested_types.end(), type) == nested.nested_types.end()) scope.nested_types.push_back(type);
        }
        out = qualify_nested(out, scope);
    }
    return out;
}

ir::Type outside(const ir::Type& t, const std::vector<Around>& around, const ir::Class& nested) {
    if (t.empty()) return t;
    const std::string spelling = outside(t.spelling, around, nested);
    if (spelling == t.spelling) return t;
    ir::Type out = ir::type_from_spelling(spelling);
    out.origin = t.origin;
    out.reason = t.reason;
    out.where = t.where;
    return out;
}

void add_nested(const ir::Class& holder, const std::string& path, std::vector<Around>& around, std::vector<ir::Class>& out) {
    for (const ir::Class& n : holder.nested_classes) {
        if (n.nested_access != ir::Access::Public || n.template_params) continue;

        ir::Class flat = n;
        flat.name = path + "::" + n.name;
        flat.enclosing = path;
        for (ir::Method& m : flat.methods) {
            m.ret = outside(m.ret, around, n);
            for (ir::Param& p : m.params) p.type = outside(p.type, around, n);
        }
        for (ir::Field& f : flat.fields) f.type = outside(f.type, around, n);
        for (ir::BaseSpec& b : flat.bases) b.name = outside(b.name, around, n);

        const std::string flat_name = flat.name;
        out.push_back(std::move(flat));
        around.push_back({flat_name, n.nested_types});
        add_nested(n, flat_name, around, out);
        around.pop_back();
    }
}

}  // namespace

ir::Model flatten_nested_classes(const ir::Model& model) {
    ir::Model out = model;
    out.classes.clear();
    for (const ir::Class& c : model.classes) {
        out.classes.push_back(c);
        std::vector<Around> around;
        around.push_back({c.name, c.nested_types});
        add_nested(c, c.name, around, out.classes);
    }
    return out;
}

}  // namespace mt::backend
