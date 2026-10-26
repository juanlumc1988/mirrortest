// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mirrortest/backend/cxx_format.h"
#include "mirrortest/backend/test_planner.h"
#include "mirrortest/core/naming.h"
#include "mirrortest/ir/type.h"

namespace mt::backend {

namespace {

// The unqualified name of a base class as written: `acme::Sensor`, `Sensor<int>` -> "Sensor".
std::string base_name(const std::string& written) {
    std::string name = written.substr(0, written.find('<'));
    const std::size_t sep = name.rfind("::");
    return sep == std::string::npos ? name : name.substr(sep + 2);
}

// What makes two declarations the same function, for deciding whether a derived
// class implements what its base left pure.
std::string signature_key(const ir::Method& m) {
    std::string key = m.name + "(";
    for (const ir::Param& p : m.params) key += p.type.spelling + ",";
    return key + (m.is_const ? ")const" : ")");
}

ir::Type qualified(const ir::Type& t, const ir::Class& owner) { return ir::type_from_spelling(qualify_nested(t.spelling, owner)); }

bool is_variadic(const ir::Method& m) {
    return std::any_of(m.params.begin(), m.params.end(), [](const ir::Param& p) { return p.type.spelling == "..."; });
}

class MockPlanner {
public:
    MockPlanner(const ir::Model& model, const PlannerOptions& options, core::Diagnostics& diagnostics)
        : model_(model), options_(options), diag_(diagnostics) {}

    std::vector<ir::MockClass> run() {
        std::vector<ir::MockClass> mocks;
        for (const ir::Class& c : model_.classes) {
            if (c.name.find('<') != std::string::npos) continue;   // an instantiation of a template (design 9.1): no mock
            Pure pure;
            std::set<const ir::Class*> visited;
            collect(c, pure, visited);
            if (pure.found.empty()) continue;

            if (c.template_params) {
                diag_.add(note("the interface '" + ir::qualified_name(c.ns, c.name) + "' is a class template; no mock is generated for it", c));
                continue;
            }
            const std::string stem = core::snake_case(c.name);
            ir::MockClass mock;
            mock.name = core::expand_pattern(options_.config.naming.mock_class, stem, c.name);
            if (declared(mock.name)) {
                diag_.add(note("the header declares '" + mock.name + "' already; no mock is generated for '" + c.name + "'", c));
                continue;
            }
            mock.file = core::expand_pattern(options_.config.naming.mock_file, stem, c.name);
            mock.base = c.name;
            mock.ns = c.ns;
            mock.header = options_.header_include;
            mock.notes = pure.notes;
            for (const Found& f : pure.found) mock.methods.push_back(method_of(f));
            for (const std::string& unknown : pure.unknown_bases) {
                mock.notes.push_back("the base '" + unknown + "' is declared elsewhere or is a template: if it has pure virtual functions, the mock must declare them too");
            }
            mocks.push_back(std::move(mock));
        }
        return mocks;
    }

private:
    struct Found {
        const ir::Class*  owner;
        const ir::Method* method;
    };

    struct Pure {
        std::vector<Found>       found;
        std::set<std::string>    decided;   // signatures a closer class implemented or already listed
        std::vector<std::string> notes;
        std::vector<std::string> unknown_bases;
    };

    core::Diagnostic note(const std::string& message, const ir::Class& c) const {
        core::Diagnostic d;
        d.severity = core::Severity::Note;
        d.code = core::code::unsupported;
        d.message = message;
        d.where = c.where;
        return d;
    }

    bool declared(const std::string& name) const {
        return std::any_of(model_.classes.begin(), model_.classes.end(), [&](const ir::Class& c) { return c.name == name; });
    }

    const ir::Class* find_base(const std::string& written) const {
        const std::string wanted = base_name(written);
        for (const ir::Class& c : model_.classes) {
            if (c.name == wanted) return &c;
        }
        return nullptr;
    }

    // The pure virtual functions of `c` and, below the ones it implements, of its bases in the
    // model. The class's own come first, then each base's, depth first, as the bases are listed.
    void collect(const ir::Class& c, Pure& pure, std::set<const ir::Class*>& visited) const {
        if (!visited.insert(&c).second) return;
        for (const ir::Method& m : c.methods) {
            if (m.kind == ir::MethodKind::Constructor || m.kind == ir::MethodKind::Destructor) continue;
            const std::string key = signature_key(m);
            if (!pure.decided.insert(key).second) continue;   // a closer class has said what this is
            if (!m.is_pure) continue;

            if (m.kind == ir::MethodKind::Operator || m.kind == ir::MethodKind::Conversion || is_variadic(m)) {
                pure.notes.push_back("'" + c.name + "::" + m.name + "' is pure virtual and gmock cannot mock " +
                                     (is_variadic(m) ? "a C-style variadic function" : "an operator") + ": declare it by hand");
                continue;
            }
            pure.found.push_back({&c, &m});
        }
        for (const ir::BaseSpec& b : c.bases) {
            const ir::Class* base = find_base(b.name);
            if (base != nullptr && !base->template_params) {
                collect(*base, pure, visited);
            } else if (std::find(pure.unknown_bases.begin(), pure.unknown_bases.end(), b.name) == pure.unknown_bases.end()) {
                pure.unknown_bases.push_back(b.name);   // not here, or a template whose functions depend on its arguments
            }
        }
    }

    static ir::MockMethod method_of(const Found& f) {
        const ir::Method& m = *f.method;
        ir::MockMethod out;
        out.name = m.name;
        out.ret = qualified(m.ret, *f.owner);
        for (const ir::Param& p : m.params) out.params.push_back(qualified(p.type, *f.owner));
        out.is_const = m.is_const;
        out.is_noexcept = m.is_noexcept;
        out.ref_qualifier = m.ref_qualifier;
        return out;
    }

    const ir::Model&      model_;
    const PlannerOptions& options_;
    core::Diagnostics&    diag_;
};

}  // namespace

std::vector<ir::MockClass> plan_mocks(const ir::Model& model, const PlannerOptions& options, core::Diagnostics& diagnostics) {
    if (!options.config.gtest.mocks || options.framework != ir::Framework::GTest) return {};
    return MockPlanner(model, options, diagnostics).run();
}

}  // namespace mt::backend
