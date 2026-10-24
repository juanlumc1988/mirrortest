// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/backend/test_planner.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mirrortest/backend/cxx_format.h"
#include "mirrortest/core/naming.h"
#include "mirrortest/ir/classify.h"
#include "mirrortest/ir/type.h"

namespace mt::backend {

namespace {

using ir::Stmt;

bool starts_with(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }
bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string join(const std::vector<std::string>& parts, const std::string& separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i == 0 ? "" : separator) + parts[i];
    return out;
}

// ---- statements -----------------------------------------------------------------------------------------------

Stmt code(std::string text, bool commented_out = false) {
    Stmt s;
    s.kind = Stmt::Kind::Code;
    s.text = std::move(text);
    s.commented_out = commented_out;
    return s;
}

Stmt comment(std::string text) {
    Stmt s;
    s.kind = Stmt::Kind::Comment;
    s.text = std::move(text);
    return s;
}

Stmt blank() {
    Stmt s;
    s.kind = Stmt::Kind::Blank;
    return s;
}

Stmt placeholder(std::string text) {
    Stmt s;
    s.kind = Stmt::Kind::Placeholder;
    s.text = std::move(text);
    return s;
}

Stmt static_assertion(std::string condition, std::string message) {
    Stmt s;
    s.kind = Stmt::Kind::StaticAssert;
    s.text = std::move(condition);
    s.message = std::move(message);
    return s;
}

// ---- names and types ------------------------------------------------------------------------------------------

// The names a test body has taken, so that a parameter called `sut` or `result`
// does not collide with the variables the skeleton adds.
class Names {
public:
    std::string reserve(const std::string& wanted) {
        std::string name = wanted.empty() ? "value" : wanted;
        if (used_.count(name) != 0) {
            for (unsigned n = 2;; ++n) {
                const std::string candidate = name + std::to_string(n);
                if (used_.count(candidate) == 0) {
                    name = candidate;
                    break;
                }
            }
        }
        used_.insert(name);
        return name;
    }

private:
    std::set<std::string> used_;
};

bool is_ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// Inside the test the class's nested types must be written `Foo::Item`: the
// header spells them `Item`, which only means something within Foo.
std::string qualify_nested(const std::string& spelling, const ir::Class& c) {
    std::string out;
    std::size_t i = 0;
    while (i < spelling.size()) {
        if (std::isdigit(static_cast<unsigned char>(spelling[i])) != 0) {
            // 3u, 0x1F, 1.5f: one token, none of it a name that could be a nested type.
            while (i < spelling.size() && (is_ident_char(spelling[i]) || spelling[i] == '.' || spelling[i] == '\'')) out += spelling[i++];
            continue;
        }
        if (!(std::isalpha(static_cast<unsigned char>(spelling[i])) != 0 || spelling[i] == '_')) {
            out += spelling[i++];
            continue;
        }
        std::size_t end = i;
        while (end < spelling.size() && is_ident_char(spelling[end])) ++end;
        const std::string word = spelling.substr(i, end - i);
        const bool scoped = !out.empty() && out.back() == ':';
        if (!scoped && std::find(c.nested_types.begin(), c.nested_types.end(), word) != c.nested_types.end()) out += c.name + "::";
        out += word;
        i = end;
    }
    return out;
}

ir::Type qualified(const ir::Type& t, const ir::Class& c) { return ir::type_from_spelling(qualify_nested(t.spelling, c)); }

// A type that cannot be written before the name of a variable: function
// pointers and references to arrays (`int(&)[3]`; an array cannot be returned
// by value), and `auto`. (As an argument it is already "not constructible";
// this is for the result, which is declared with `auto`.)
bool is_exotic(const ir::Type& t) { return t.spelling.find('(') != std::string::npos || t.base == "auto"; }

// The type of a local that can be passed for a parameter: the spelling without
// the reference and without the top-level const (`const std::string&` ->
// `std::string`, `char* const` -> `char*`, `const char*` stays).
std::string local_type(const ir::Type& t) {
    std::string s = t.spelling;
    while (!s.empty() && s.back() == '&') s.pop_back();
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (ends_with(s, " const")) {
        s.erase(s.size() - 6);
    } else if (t.pointer_depth == 0 && starts_with(s, "const ")) {
        s.erase(0, 6);
    }
    return s;
}

// `const R result` for a value, `R result` for a reference, `R const result`
// for a pointer: a plain "const" in front would make the pointee const instead.
std::string result_declaration(const ir::Type& t) {
    if (is_exotic(t)) return "auto result";
    if (t.ref != ir::RefKind::None) return t.spelling + " result";
    if (t.pointer_depth > 0) return t.spelling + (ends_with(t.spelling, " const") ? "" : " const") + " result";
    if (starts_with(t.spelling, "const ")) return t.spelling + " result";
    return "const " + t.spelling + " result";
}

// The type a conversion operator converts to: "operator bool" -> "bool".
ir::Type converted_type(const ir::Method& m) {
    return ir::type_from_spelling(m.name.size() > 9 ? m.name.substr(9) : std::string("auto"));
}

// ---- what the planner does ------------------------------------------------------------------------------------

// The arguments for a call. When one of them cannot be built (`!ok`) the locals
// and the expressions are still planned, for the caller to comment out: the
// commented call that results names every argument, and the person writing the
// test only has to replace the line of the one that is missing.
struct Args {
    std::vector<Stmt>        locals;
    std::vector<std::string> expressions;
    bool                     ok = true;
    bool                     uses_move = false;   // meaningful only if ok: a commented-out call needs no <utility>
    std::string              left_out;   // what the call does not pass, for a TODO: "the arguments that have default values"
    std::string              why;       // when !ok: the first argument that cannot be built
};

// The object under test. A constructor that takes `T&&` is moved into here
// and in the constructor's own case, which every public constructor gets: that
// case is what asks for <utility>, so no flag is kept for it here.
struct Sut {
    bool              available = false;
    std::vector<Stmt> setup;           // locals for the constructor's arguments; empty unless available
    std::string       declaration;     // "Foo sut" or "Foo sut(arg1, arg2)"
    std::string       why;             // when !available
};

class Planner {
public:
    Planner(const ir::Model& model, const PlannerOptions& options, core::Diagnostics& diagnostics)
        : model_(model), options_(options), diag_(diagnostics) {
        models_.push_back(&model_);
    }

    std::vector<ir::TestPlan> run() {
        std::vector<ir::TestPlan> plans;
        for (const ir::Class& c : model_.classes) {
            if (c.template_params) {
                diag_.warning(core::code::unsupported,
                              "the class template '" + ir::qualified_name(c.ns, c.name) +
                                  "' is skipped: tests for templates need instantiations, which are not supported yet",
                              c.where);
                continue;
            }
            std::vector<const ir::Method*> testable = testable_methods(c);
            if (testable.empty()) {
                core::Diagnostic d;
                d.severity = core::Severity::Note;
                d.code = core::code::unsupported;
                d.message = "the class '" + ir::qualified_name(c.ns, c.name) + "' has nothing public to test; no tests are generated for it";
                d.where = c.where;
                diag_.add(std::move(d));
                continue;
            }
            plans.push_back(plan_class(c, testable));
        }
        return plans;
    }

private:
    // Public, not deleted, not a signal (a signal has no body to call), not a template.
    static std::vector<const ir::Method*> testable_methods(const ir::Class& c) {
        std::vector<const ir::Method*> out;
        for (const ir::Method& m : c.methods) {
            if (m.access != ir::Access::Public || m.is_deleted || m.is_template || m.qt_role == ir::QtRole::Signal) continue;
            out.push_back(&m);
        }
        return out;
    }

    static bool is_abstract(const ir::Class& c) {
        return std::any_of(c.methods.begin(), c.methods.end(), [](const ir::Method& m) { return m.is_pure; });
    }

    bool constructible(const ir::Type& t) const {
        return ir::is_default_constructible(t, models_, options_.config.default_constructible_types);
    }

    // ---- arguments --------------------------------------------------------------------------------------------

    Args make_args(const ir::Class& c, const std::vector<ir::Param>& params, Names& names) const {
        Args args;
        for (std::size_t i = 0; i < params.size(); ++i) {
            const ir::Param& p = params[i];
            if (p.default_value) {
                args.left_out = "the arguments that have default values are left out";   // the rest follow suit
                break;
            }
            if (p.type.spelling == "...") {
                args.left_out = "the variadic arguments are left out";
                break;
            }
            const ir::Type type = qualified(p.type, c);
            if (args.ok && !constructible(type)) {
                args.ok = false;
                args.why = "an argument of type '" + local_type(type) + "' cannot be built here";
            }
            const std::string name = names.reserve(p.name.empty() ? "arg" + std::to_string(i + 1) : p.name);
            args.locals.push_back(code(local_type(type) + " " + name + "{}"));
            if (type.ref == ir::RefKind::RValue) {
                args.expressions.push_back("std::move(" + name + ")");
                args.uses_move = true;
            } else {
                args.expressions.push_back(name);
            }
        }
        return args;
    }

    // ---- the object under test ----------------------------------------------------------------------------

    // `Foo sut;` if Foo can be default-constructed; else the public constructor
    // with the fewest mandatory parameters that can be called with locals.
    Sut make_sut(const ir::Class& c, Names& names, const std::string& sut_name) const {
        Sut sut;
        if (is_abstract(c)) {
            sut.why = "'" + c.name + "' is abstract: derive a test double and build that";
            return sut;
        }
        if (constructible(ir::type_from_spelling(c.name))) {
            sut.available = true;
            sut.declaration = c.name + " " + sut_name;
            return sut;
        }

        const ir::Method* best = nullptr;
        Args best_args;
        std::size_t best_count = 0;
        for (const ir::Method& m : c.methods) {
            if (m.kind != ir::MethodKind::Constructor || m.access != ir::Access::Public || m.is_deleted) continue;
            Names scratch = names;
            const Args args = make_args(c, m.params, scratch);
            if (!args.ok) continue;
            const std::size_t count = args.expressions.size();
            if (best == nullptr || count < best_count) {
                best = &m;
                best_args = args;
                best_count = count;
            }
        }
        if (best == nullptr) {
            sut.why = "'" + c.name + "' has no public constructor that can be called with default values: build sut by hand";
            return sut;
        }
        // Take the names for real now that the constructor is chosen.
        const Args args = make_args(c, best->params, names);
        sut.available = true;
        sut.setup = args.locals;
        sut.declaration = c.name + " " + sut_name + "(" + join(args.expressions, ", ") + ")";
        return sut;
    }

    // ---- one case -------------------------------------------------------------------------------------------------

    // The locals a call needs, commented out together with the call when it
    // cannot be compiled: an unused local would be a warning, and the generated
    // file is built with the project's own flags.
    static void add_locals(std::vector<Stmt>& to, const std::vector<Stmt>& from, bool commented_out) {
        for (Stmt s : from) {
            s.commented_out = commented_out;
            to.push_back(std::move(s));
        }
    }

    ir::TestCase plan_case(const ir::Class& c, const ir::Method& m, const std::string& name) {
        ir::TestCase tc;
        tc.name = name;
        tc.target = method_signature(c, m);
        Names names;
        names.reserve("sut");
        names.reserve("const_sut");
        names.reserve("result");
        uses_move_ = false;

        tc.body.push_back(placeholder("TODO(mt): write the test for " + tc.target));
        tc.body.push_back(blank());
        tc.body.push_back(comment("Arrange"));

        if (m.kind == ir::MethodKind::Constructor) {
            plan_constructor(c, m, names, tc.body);
        } else if (m.kind == ir::MethodKind::Destructor) {
            plan_destructor(c, names, tc.body);
        } else {
            plan_call(c, m, names, tc.body, tc.target);
        }
        return tc;
    }

    void plan_constructor(const ir::Class& c, const ir::Method& m, Names& names, std::vector<Stmt>& body) {
        const Args args = make_args(c, m.params, names);
        const bool buildable = args.ok && !is_abstract(c);
        add_locals(body, args.locals, !buildable);
        uses_move_ = uses_move_ || (buildable && args.uses_move);
        body.push_back(comment("Act"));
        if (!args.ok) body.push_back(comment("TODO(mt): " + args.why));
        if (is_abstract(c)) body.push_back(comment("TODO(mt): '" + c.name + "' is abstract: construct a class derived from it instead"));
        if (!args.left_out.empty()) body.push_back(comment("TODO(mt): " + args.left_out));
        const std::string declaration = args.expressions.empty() ? c.name + " sut" : c.name + " sut(" + join(args.expressions, ", ") + ")";
        body.push_back(code(declaration, !buildable));
        body.push_back(code("(void)sut", !buildable));
        body.push_back(comment("Assert"));
        body.push_back(comment("TODO(mt): check the state of sut"));
    }

    void plan_destructor(const ir::Class& c, Names& names, std::vector<Stmt>& body) {
        const Sut sut = make_sut(c, names, "sut");
        add_locals(body, sut.setup, false);
        if (!sut.available) body.push_back(comment("TODO(mt): " + sut.why));
        body.push_back(code(sut.available ? sut.declaration : c.name + " sut", !sut.available));
        body.push_back(comment("Act"));
        body.push_back(comment("sut is destroyed when the test ends"));
        body.push_back(comment("Assert"));
        body.push_back(comment("TODO(mt): check what the destructor releases"));
    }

    void plan_call(const ir::Class& c, const ir::Method& m, Names& names, std::vector<Stmt>& body, const std::string& target) {
        Sut sut;
        sut.available = true;
        if (!m.is_static) sut = make_sut(c, names, "sut");
        const Args args = make_args(c, m.params, names);
        const bool buildable = sut.available && args.ok;

        if (!m.is_static) {
            add_locals(body, sut.setup, !buildable);
            if (!sut.available) body.push_back(comment("TODO(mt): " + sut.why));
            body.push_back(code(sut.available ? sut.declaration : c.name + " sut", !buildable));
        }
        if (!args.ok) body.push_back(comment("TODO(mt): " + args.why));
        add_locals(body, args.locals, !buildable);
        uses_move_ = uses_move_ || (buildable && args.uses_move);
        if (!args.left_out.empty()) body.push_back(comment("TODO(mt): " + args.left_out));

        body.push_back(comment("Act"));
        std::string object = "sut";
        if (m.is_const) {
            body.push_back(code("const " + c.name + "& const_sut = sut", !buildable));
            object = "const_sut";
        }
        const std::string call = (m.is_static ? c.name + "::" : object + ".") + m.name + "(" + join(args.expressions, ", ") + ")";
        const ir::Type ret = m.kind == ir::MethodKind::Conversion ? converted_type(m) : qualified(m.ret, c);
        const bool returns_value = !ret.empty() && !ret.is_void();
        if (returns_value) {
            body.push_back(code(result_declaration(ret) + " = " + call, !buildable));
            body.push_back(code("(void)result", !buildable));
        } else {
            body.push_back(code(call, !buildable));
        }

        body.push_back(comment("Assert"));
        if (m.is_noexcept) {
            body.push_back(static_assertion("noexcept(" + call + ")", target + " is declared noexcept"));
            if (!buildable) body.back().commented_out = true;
        }
        body.push_back(comment(returns_value ? "TODO(mt): compare result with the expected value" : "TODO(mt): check the effect of the call"));
    }

    // ---- one class ------------------------------------------------------------------------------------------------

    ir::TestPlan plan_class(const ir::Class& c, const std::vector<const ir::Method*>& testable) {
        std::vector<core::TestNameInput> inputs;
        for (const ir::Method* m : testable) {
            core::TestNameInput in;
            in.base = m->kind == ir::MethodKind::Constructor ? "Constructs"
                      : m->kind == ir::MethodKind::Destructor ? "Destructs"
                                                              : core::test_base_name(m->name);
            for (const ir::Param& p : m->params) in.param_types.push_back(p.type.spelling);
            inputs.push_back(std::move(in));
        }
        const std::vector<std::string> names = core::unique_test_names(inputs);

        ir::TestSuite suite;
        suite.name = core::expand_pattern(options_.config.naming.suite, core::snake_case(c.name), c.name);
        suite.target_class = c.name;
        suite.ns = c.ns;
        bool moves = false;
        for (std::size_t i = 0; i < testable.size(); ++i) {
            suite.cases.push_back(plan_case(c, *testable[i], names[i]));
            moves = moves || uses_move_;
        }

        ir::TestPlan plan;
        plan.file = core::expand_pattern(options_.config.naming.gtest_file, core::snake_case(c.name), c.name);
        plan.includes.push_back({"gtest/gtest.h", true, {}});
        plan.includes.push_back({options_.header_include, false, {}});
        if (moves) plan.includes.push_back({"utility", true, {}});
        plan.suites.push_back(std::move(suite));
        return plan;
    }

    const ir::Model&              model_;
    const PlannerOptions&         options_;
    core::Diagnostics&            diag_;
    std::vector<const ir::Model*> models_;
    bool                          uses_move_ = false;
};

}  // namespace

std::vector<ir::TestPlan> plan_tests(const ir::Model& model, const PlannerOptions& options, core::Diagnostics& diagnostics) {
    return Planner(model, options, diagnostics).run();
}

}  // namespace mt::backend
