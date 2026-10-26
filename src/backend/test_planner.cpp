// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/backend/test_planner.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mirrortest/backend/cxx_format.h"
#include "mirrortest/backend/template_expand.h"
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

Stmt data_marker() {
    Stmt s;
    s.kind = Stmt::Kind::Data;
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

ir::Type qualified(const ir::Type& t, const ir::Class& c) { return ir::type_from_spelling(qualify_nested(t.spelling, c)); }

// What `{Class}` stands for in a name: "Box<int>" is "Box_int", "Box<std::string, 8>" is "Box_std_string_8".
std::string identifier(const std::string& class_name) {
    std::string out;
    bool pending = false;
    for (char ch : class_name) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            if (pending && !out.empty()) out += '_';
            pending = false;
            out += ch;
        } else {
            pending = true;
        }
    }
    return out;
}

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
    std::vector<std::string> names;         // of the locals, in the same order
    std::vector<std::string> expressions;
    bool                     ok = true;
    bool                     uses_move = false;   // meaningful only if ok: a commented-out call needs no <utility>
    std::string              left_out;   // what the call does not pass, for a TODO: "the arguments that have default values"
    std::string              why;       // when !ok: the first argument that cannot be built
    std::vector<std::string> mock_headers;   // of the mocks that stand in for interfaces here (gtest.mocks)
    std::vector<ir::Column>  columns;        // the locals, as a table's columns, when every one of them can be a column
    bool                     tabular = true; // false once an argument is not a plain local (a mock, an rvalue reference)
};

// The object under test. A constructor that takes `T&&` is moved into here
// and in the constructor's own case, which every public constructor gets: that
// case is what asks for <utility>, so no flag is kept for it here.
struct Sut {
    bool                     available = false;
    std::vector<Stmt>        setup;         // locals for the constructor's arguments; empty unless available
    std::vector<std::string> names;         // of those locals
    std::string              declaration;   // "Foo sut" or "Foo sut(arg1, arg2)"
    std::string              initialiser;   // "sut(arg1, arg2)" for a fixture's constructor; empty if Foo is default constructed
    std::string              why;           // when !available
    std::vector<std::string> mock_headers;  // as for Args
};

class Planner {
public:
    Planner(const ir::Model& model, const PlannerOptions& options, core::Diagnostics& diagnostics)
        : model_(model), options_(options), diag_(diagnostics) {
        models_.push_back(&model_);
        // The mocks gtest.mocks writes are also what an interface argument is built with. What
        // planning them has to say is said once, by whoever asks for them as files.
        core::Diagnostics quiet;
        mocks_ = plan_mocks(model_, options_, quiet);
    }

    std::vector<ir::TestPlan> run() {
        std::vector<ir::TestPlan> plans;
        for (const ir::Class& c : model_.classes) {
            if (c.template_params) {
                diag_.warning(core::code::unsupported,
                              "the class template '" + ir::qualified_name(c.ns, c.name) +
                                  "' is skipped: list the instantiations to test in template_instantiations (\"" +
                                  ir::qualified_name(c.ns, c.name) + "\": [\"int\"])",
                              c.where);
                continue;
            }
            std::vector<const ir::Method*> testable = testable_methods(c);
            if (testable.empty() && c.bases.empty()) {
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
    // Public, not deleted, not a template. A signal has no body to call: GoogleTest has
    // nothing to say about it, Qt Test listens to it with a QSignalSpy.
    std::vector<const ir::Method*> testable_methods(const ir::Class& c) const {
        std::vector<const ir::Method*> out;
        for (const ir::Method& m : c.methods) {
            if (m.access != ir::Access::Public || m.is_deleted || m.is_template) continue;
            if (m.qt_role == ir::QtRole::Signal && !qtest()) continue;
            out.push_back(&m);
        }
        return out;
    }

    bool qtest() const { return options_.framework == ir::Framework::QTest; }

    // A fixture is a GoogleTest idea; Qt Test's counterpart (init(), cleanup()) is not planned yet.
    bool fixture() const { return options_.config.gtest.fixture && !qtest(); }

    static bool is_abstract(const ir::Class& c) {
        return std::any_of(c.methods.begin(), c.methods.end(), [](const ir::Method& m) { return m.is_pure; });
    }

    bool constructible(const ir::Type& t) const {
        return ir::is_default_constructible(t, models_, options_.config.default_constructible_types);
    }

    // ---- mocks as arguments -----------------------------------------------------------------------------------

    // How a parameter that takes an interface is filled with the mock of it (gtest.mocks).
    struct MockUse {
        enum class Kind { Local, Owned, Shared };
        Kind        kind = Kind::Local;
        bool        address = false;   // `&sensor` for a pointer
        std::string type;              // "testing::NiceMock<MockSensor>"
        std::string interface;         // as the parameter writes it, for a smart pointer: "Sensor"
        std::string header;
    };

    static std::string without_scope(const std::string& name) {
        const std::size_t sep = name.rfind("::");
        return sep == std::string::npos ? name : name.substr(sep + 2);
    }

    // The mock whose interface is called `name`, if there is exactly one that can be built: one that
    // says it leaves something out (an operator, a base it cannot see) is still abstract. Two interfaces
    // of that name in different namespaces: the one in the class's own, else none.
    const ir::MockClass* find_mock(const std::string& name, const ir::Class& c) const {
        const std::string wanted = without_scope(name);
        std::vector<const ir::MockClass*> found;
        for (const ir::MockClass& m : mocks_) {
            if (m.base == wanted) found.push_back(&m);
        }
        if (found.size() > 1) {
            std::vector<const ir::MockClass*> same_namespace;
            for (const ir::MockClass* m : found) {
                if (m->ns == c.ns) same_namespace.push_back(m);
            }
            found = same_namespace;
        }
        if (found.size() != 1 || !found[0]->notes.empty()) return nullptr;
        return found[0];
    }

    std::optional<MockUse> mock_for(const ir::Type& t, const ir::Class& c) const {
        std::string interface = t.base;
        MockUse use;
        if (t.pointer_depth == 0 && (t.ref == ir::RefKind::None || (t.ref == ir::RefKind::LValue && t.is_const)) &&
            (starts_with(t.base, "std::unique_ptr<") || starts_with(t.base, "std::shared_ptr<")) && ends_with(t.base, ">")) {
            use.kind = starts_with(t.base, "std::unique_ptr<") ? MockUse::Kind::Owned : MockUse::Kind::Shared;
            const std::size_t open = t.base.find('<');
            interface = t.base.substr(open + 1, t.base.size() - open - 2);
        } else if (t.pointer_depth == 1 && t.ref == ir::RefKind::None) {
            use.address = true;
        } else if (!(t.pointer_depth == 0 && t.ref == ir::RefKind::LValue)) {
            return std::nullopt;
        }
        const ir::MockClass* mock = find_mock(interface, c);
        if (mock == nullptr) return std::nullopt;
        use.type = "testing::NiceMock<" + (mock->ns == c.ns ? mock->name : ir::qualified_name(mock->ns, mock->name)) + ">";
        use.interface = interface;
        use.header = mock->file;
        return use;
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
            if (const std::optional<MockUse> mock = mock_for(type, c)) {
                args.mock_headers.push_back(mock->header);
                args.tabular = false;
                if (mock->kind == MockUse::Kind::Owned) {
                    args.expressions.push_back("std::unique_ptr<" + mock->interface + ">(new " + mock->type + ")");
                } else if (mock->kind == MockUse::Kind::Shared) {
                    args.expressions.push_back("std::make_shared<" + mock->type + ">()");
                } else {
                    const std::string name = names.reserve(p.name.empty() ? "arg" + std::to_string(i + 1) : p.name);
                    args.locals.push_back(code(mock->type + " " + name));
                    args.names.push_back(name);
                    args.expressions.push_back(mock->address ? "&" + name : name);
                }
                continue;
            }
            if (args.ok && !constructible(type)) {
                args.ok = false;
                args.why = "an argument of type '" + local_type(type) + "' cannot be built here";
            }
            const std::string name = names.reserve(p.name.empty() ? "arg" + std::to_string(i + 1) : p.name);
            args.locals.push_back(code(local_type(type) + " " + name + "{}"));
            args.names.push_back(name);
            args.columns.push_back({ir::type_from_spelling(local_type(type)), name});
            // `&&` is moved from the local; so is a by-value argument that cannot be copied.
            if (type.ref == ir::RefKind::RValue || (type.ref == ir::RefKind::None && type.pointer_depth == 0 && !copyable(type))) {
                args.expressions.push_back("std::move(" + name + ")");
                args.uses_move = true;
                args.tabular = false;
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
        std::size_t best_count = 0;
        for (const ir::Method& m : c.methods) {
            if (m.kind != ir::MethodKind::Constructor || m.access != ir::Access::Public || m.is_deleted) continue;
            Names scratch = names;
            const Args args = make_args(c, m.params, scratch);
            if (!args.ok) continue;
            const std::size_t count = args.expressions.size();
            if (best == nullptr || count < best_count) {
                best = &m;
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
        sut.names = args.names;
        sut.mock_headers = args.mock_headers;
        sut.declaration = c.name + " " + sut_name + "(" + join(args.expressions, ", ") + ")";
        sut.initialiser = sut_name + "(" + join(args.expressions, ", ") + ")";
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
        if (fixture_sut_) {
            // The fixture's members are in scope in the body: a local of the same name would shadow one.
            names.reserve("object");
            for (const std::string& member : fixture_sut_->names) names.reserve(member);
        }
        // GoogleTest reads a row into a local called param before the locals of the arguments.
        if (tabulating() && !qtest()) names.reserve("param");
        uses_move_ = false;

        tc.body.push_back(placeholder("TODO(mt): write the test for " + tc.target));
        tc.body.push_back(blank());
        tc.body.push_back(comment("Arrange"));

        if (m.kind == ir::MethodKind::Constructor) {
            plan_constructor(c, m, names, tc.body);
        } else if (m.kind == ir::MethodKind::Destructor) {
            plan_destructor(c, names, tc.body);
        } else if (m.qt_role == ir::QtRole::Signal) {
            plan_signal(c, m, names, tc.body);
        } else {
            plan_call(c, m, names, tc.body, tc.target);
        }
        tc.data = std::move(data_);
        data_.reset();   // moved from is not empty: the next case must not inherit the table
        return tc;
    }

    void use_mocks(const std::vector<std::string>& headers) { mock_headers_.insert(headers.begin(), headers.end()); }

    // ---- cases over a table of arguments --------------------------------------------------------------------------

    // gtest.parameterised, or qtest.data_driven.
    bool tabulating() const { return qtest() ? options_.config.qtest.data_driven : options_.config.gtest.parameterised; }

    // A row is copied out of the table into the test (GoogleTest copies the parameter, Qt Test the
    // QVariant): what cannot be copied cannot be in one. A class that deletes its copy constructor is
    // known from the header; a smart pointer or a lock, by name.
    bool copyable(const ir::Type& t) const {
        for (const char* no : {"unique_ptr", "std::atomic", "std::mutex", "std::thread", "std::condition_variable"}) {
            if (t.base.find(no) != std::string::npos) return false;
        }
        for (const ir::Class& k : model_.classes) {
            if (k.name != without_scope(t.base)) continue;
            for (const ir::Method& m : k.methods) {
                if (m.kind == ir::MethodKind::Constructor && m.is_deleted && m.params.size() == 1 && m.params[0].type.ref == ir::RefKind::LValue &&
                    without_scope(m.params[0].type.base) == k.name) {
                    return false;
                }
            }
        }
        return true;
    }

    // What a column can be: a type that can be copied and, for Qt Test, one that QFETCH(type, name)
    // can spell -- a comma would end its first argument -- and that QTest::addColumn accepts, which
    // is not `const char*` (a static_assert: a column is a QVariant, use QString or QByteArray).
    // (Arrays and function pointers are not arguments the skeleton can build in the first place.)
    bool column_ok(const ir::Type& t) const {
        if (!copyable(t)) return false;
        return !(qtest() && (t.spelling.find(',') != std::string::npos || t.spelling == "const char*"));
    }

    void plan_constructor(const ir::Class& c, const ir::Method& m, Names& names, std::vector<Stmt>& body) {
        const Args args = make_args(c, m.params, names);
        use_mocks(args.mock_headers);
        const bool buildable = args.ok && !is_abstract(c);
        if (!args.ok) body.push_back(comment("TODO(mt): " + args.why));
        if (is_abstract(c)) body.push_back(comment("TODO(mt): '" + c.name + "' is abstract: construct a class derived from it instead"));
        add_locals(body, args.locals, !buildable);
        uses_move_ = uses_move_ || (buildable && args.uses_move);
        if (!args.left_out.empty()) body.push_back(comment("TODO(mt): " + args.left_out));
        body.push_back(comment("Act"));
        // In a fixture sut is a member that already exists; what this test builds is another object.
        const std::string var = fixture_sut_ ? "object" : "sut";
        const std::string declaration = args.expressions.empty() ? c.name + " " + var : c.name + " " + var + "(" + join(args.expressions, ", ") + ")";
        body.push_back(code(declaration, !buildable));
        body.push_back(code("(void)" + var, !buildable));
        body.push_back(comment("Assert"));
        body.push_back(comment("TODO(mt): check the state of " + var));
    }

    void plan_destructor(const ir::Class& c, Names& names, std::vector<Stmt>& body) {
        if (fixture_sut_) {
            // The fixture builds sut before the test and destroys it after: nothing to arrange or to do.
            body.push_back(comment("Act"));
            body.push_back(comment("the fixture destroys sut when the test ends"));
            body.push_back(comment("Assert"));
            body.push_back(comment("TODO(mt): check what the destructor releases"));
            return;
        }
        const Sut sut = make_sut(c, names, "sut");
        use_mocks(sut.mock_headers);
        add_locals(body, sut.setup, false);
        if (!sut.available) body.push_back(comment("TODO(mt): " + sut.why));
        body.push_back(code(sut.available ? sut.declaration : c.name + " sut", !sut.available));
        body.push_back(comment("Act"));
        body.push_back(comment("sut is destroyed when the test ends"));
        // A trivial object that nothing uses is an unused variable, and a warning.
        body.push_back(code("(void)sut", !sut.available));
        body.push_back(comment("Assert"));
        body.push_back(comment("TODO(mt): check what the destructor releases"));
    }

    // A signal is not called, it is listened to: a QSignalSpy on sut. What makes sut
    // emit it is what the person writing the test knows and the tool does not.
    void plan_signal(const ir::Class& c, const ir::Method& m, Names& names, std::vector<Stmt>& body) {
        names.reserve("spy");
        const Sut sut = make_sut(c, names, "sut");
        use_mocks(sut.mock_headers);
        add_locals(body, sut.setup, false);   // empty unless sut is available
        if (!sut.available) body.push_back(comment("TODO(mt): " + sut.why));
        body.push_back(code(sut.available ? sut.declaration : c.name + " sut", !sut.available));

        std::string member = "&" + c.name + "::" + m.name;
        // Any method of the same name makes &Class::name ambiguous: a slot with other parameters too.
        const bool overloaded = std::count_if(c.methods.begin(), c.methods.end(),
                                              [&](const ir::Method& other) { return other.name == m.name; }) > 1;
        if (overloaded) {
            std::vector<std::string> types;
            for (const ir::Param& p : m.params) types.push_back(qualify_nested(p.type.spelling, c));
            member = "static_cast<void (" + c.name + "::*)(" + join(types, ", ") + ")>(" + member + ")";
        }
        body.push_back(code("QSignalSpy spy(&sut, " + member + ")", !sut.available));
        body.push_back(code("(void)spy", !sut.available));
        body.push_back(comment("Act"));
        body.push_back(comment("TODO(mt): make sut emit the signal"));
        body.push_back(comment("Assert"));
        body.push_back(comment("TODO(mt): check spy.count() and the arguments of spy.takeFirst()"));
    }

    void plan_call(const ir::Class& c, const ir::Method& m, Names& names, std::vector<Stmt>& body, const std::string& target) {
        Sut sut;
        sut.available = true;
        if (!m.is_static) sut = fixture_sut_ ? *fixture_sut_ : make_sut(c, names, "sut");
        const Args args = make_args(c, m.params, names);
        use_mocks(sut.mock_headers);
        use_mocks(args.mock_headers);
        const bool buildable = sut.available && args.ok;

        if (!m.is_static && !fixture_sut_) {
            add_locals(body, sut.setup, !buildable);
            if (!sut.available) body.push_back(comment("TODO(mt): " + sut.why));
            body.push_back(code(sut.available ? sut.declaration : c.name + " sut", !buildable));
        }
        if (!args.ok) body.push_back(comment("TODO(mt): " + args.why));
        const bool tabulate = tabulating() && buildable && args.tabular && !args.columns.empty() &&
                              std::all_of(args.columns.begin(), args.columns.end(), [&](const ir::Column& k) { return column_ok(k.type); });
        if (tabulate) {
            body.push_back(data_marker());
            data_ = ir::DataTable{args.columns};
        } else {
            add_locals(body, args.locals, !buildable);
        }
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

    // A slot called init() is run before every test (design 9.5): a method of that name is
    // tested by a slot called init_call(), which Qt Test knows nothing about.
    static std::vector<std::string> without_qtest_special_names(std::vector<std::string> names) {
        std::set<std::string> taken(names.begin(), names.end());
        for (std::string& name : names) {
            if (!core::is_qtest_special_name(name)) continue;
            std::string candidate = name + "_call";
            for (unsigned n = 2; taken.count(candidate) != 0; ++n) candidate = name + "_call_" + std::to_string(n);
            taken.insert(candidate);
            name = candidate;
        }
        return names;
    }

    // The fixture that owns sut: the arguments its constructor needs as members, then sut, built
    // by the fixture's constructor so that the argument members are there when it runs (members are
    // initialised in the order they are declared). Where sut cannot be built the members are
    // comments and every case that needs sut is a comment too.
    ir::Fixture plan_fixture(const ir::Class& c, const std::string& name) {
        Names reserved;
        for (const char* used : {"sut", "const_sut", "result", "object"}) reserved.reserve(used);
        Sut sut = make_sut(c, reserved, "sut");
        use_mocks(sut.mock_headers);

        ir::Fixture fixture;
        fixture.name = name;
        if (sut.available) {
            fixture.members = sut.setup;
            fixture.members.push_back(code(c.name + " sut"));
            if (!sut.initialiser.empty()) fixture.initialisers.push_back(sut.initialiser);
        } else {
            fixture.members.push_back(comment("TODO(mt): " + sut.why));
            fixture.members.push_back(code(c.name + " sut", true));
        }
        fixture.set_up.push_back(comment("TODO(mt): bring sut to the state every test starts from"));
        fixture_sut_ = std::move(sut);
        return fixture;
    }

    // What a derived class has of its own that a test can check without running anything: it
    // derives from what the header says. What it inherits is not repeated (design 9.1).
    ir::TestCase plan_inherits(const ir::Class& c, const std::string& name) {
        const std::string qualified_class = ir::qualified_name(c.ns, c.name);
        ir::TestCase tc;
        tc.name = name;
        tc.target = qualified_class;
        tc.body.push_back(comment("Assert"));
        std::string listed;
        for (const ir::BaseSpec& b : c.bases) {
            tc.body.push_back(static_assertion("std::is_base_of<" + b.name + ", " + c.name + ">::value", qualified_class + " must derive from " + b.name));
            listed += (listed.empty() ? "" : ", ") + b.name;
        }
        tc.body.push_back(comment("The members inherited from " + listed + " are tested with the class that declares them, not here"));
        return tc;
    }

    ir::TestPlan plan_class(const ir::Class& c, const std::vector<const ir::Method*>& testable) {
        std::vector<core::TestNameInput> inputs;
        const bool inherits = !c.bases.empty();
        if (inherits) {
            core::TestNameInput in;
            in.base = "Inherits";
            inputs.push_back(std::move(in));
        }
        for (const ir::Method* m : testable) {
            core::TestNameInput in;
            in.base = m->kind == ir::MethodKind::Constructor ? "Constructs"
                      : m->kind == ir::MethodKind::Destructor ? "Destructs"
                                                              : core::test_base_name(m->name);
            for (const ir::Param& p : m->params) in.param_types.push_back(p.type.spelling);
            inputs.push_back(std::move(in));
        }
        std::vector<std::string> names = core::unique_test_names(inputs);
        if (qtest()) names = without_qtest_special_names(std::move(names));

        ir::TestSuite suite;
        suite.name = core::expand_pattern(qtest() ? options_.config.naming.qtest_class : options_.config.naming.suite,
                                          core::snake_case(c.name), identifier(c.name));
        suite.target_class = c.name;
        suite.ns = c.ns;
        bool moves = false;
        mock_headers_.clear();
        if (fixture()) suite.fixture = plan_fixture(c, suite.name);
        const std::size_t first_method = inherits ? 1 : 0;
        if (inherits) suite.cases.push_back(plan_inherits(c, names[0]));
        for (std::size_t i = 0; i < testable.size(); ++i) {
            suite.cases.push_back(plan_case(c, *testable[i], names[first_method + i]));
            moves = moves || uses_move_;
        }

        ir::TestPlan plan;
        plan.file = core::expand_pattern(qtest() ? options_.config.naming.qtest_file : options_.config.naming.gtest_file,
                                         core::snake_case(c.name), identifier(c.name));
        plan.includes.push_back({qtest() ? "QtTest" : "gtest/gtest.h", true, {}});
        plan.includes.push_back({options_.header_include, false, {}});
        if (moves) plan.includes.push_back({"utility", true, {}});
        if (inherits) plan.includes.push_back({"type_traits", true, {}});
        for (const std::string& header : mock_headers_) plan.includes.push_back({header, false, {}});
        plan.suites.push_back(std::move(suite));
        return plan;
    }

    const ir::Model&              model_;
    const PlannerOptions&         options_;
    core::Diagnostics&            diag_;
    std::vector<const ir::Model*> models_;
    bool                          uses_move_ = false;
    std::optional<ir::DataTable>  data_;           // of the case being planned, if it runs over a table
    std::vector<ir::MockClass>    mocks_;          // gtest.mocks: what an interface argument is built with
    std::set<std::string>         mock_headers_;   // of the mocks the class being planned uses
    std::optional<Sut>            fixture_sut_;   // set by plan_fixture() for each class; read only when fixture() is on
};

}  // namespace

std::vector<ir::TestPlan> plan_tests(const ir::Model& model, const PlannerOptions& options, core::Diagnostics& diagnostics) {
    const ir::Model instantiated = instantiate_templates(model, options.config, diagnostics);
    return Planner(instantiated, options, diagnostics).run();
}

}  // namespace mt::backend
