// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/frontend/classify.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <tuple>

#include "mirrortest/frontend/known_names.h"
#include "mirrortest/ir/type.h"

namespace mt::frontend {

std::string ClassTarget::qualified() const { return ir::qualified_name(ns, name); }
std::string FunctionTarget::qualified() const { return ir::qualified_name(ns, name); }

namespace {

using Ns = std::vector<std::string>;

constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

bool before(const ir::SourceLoc& a, const ir::SourceLoc& b) {
    return std::tie(a.line, a.column) < std::tie(b.line, b.column);
}

std::string join(const Ns& parts) {
    std::string out;
    for (const std::string& p : parts) {
        if (!out.empty()) out += "::";
        out += p;
    }
    return out;
}

Ns split_scope(const std::string& text) {
    Ns out;
    std::string_view rest = text;
    if (rest.compare(0, 2, "::") == 0) rest.remove_prefix(2);
    for (std::size_t sep; (sep = rest.find("::")) != std::string_view::npos;) {
        out.emplace_back(rest.substr(0, sep));
        rest.remove_prefix(sep + 2);
    }
    if (!rest.empty()) out.emplace_back(rest);
    return out;
}

bool ends_with(const Ns& whole, const Ns& tail) {
    return tail.size() <= whole.size() && std::equal(tail.begin(), tail.end(), whole.end() - tail.size());
}

bool starts_upper(const std::string& s) {
    return !s.empty() && std::isupper(static_cast<unsigned char>(s[0])) != 0;
}

void note_first_use(ir::SourceLoc& first, const ir::SourceLoc& where) {
    if (!first.known() || (where.known() && before(where, first))) first = where;
}

// One mention of a class in the test: a declared type, a construction, a call
// receiver, a field receiver. Gathered from all kinds of fact and ordered by
// position, so that what is found first in the source is found first here.
struct Occurrence {
    enum class Kind { Type, Construction, Method, Field };
    Kind          kind = Kind::Type;
    std::size_t   index = 0;
    TypeName      type;
    Ns            test_ns;
    ir::SourceLoc where;
};

struct KnownType {
    Ns          ns;
    std::string name;
};

class Classifier {
public:
    Classifier(TestFileFacts facts, const ClassifyOptions& options, core::Diagnostics& diagnostics)
        : facts_(std::move(facts)), options_(options), diag_(diagnostics) {}

    Classified run() {
        index_environment();
        collect_classes();
        resolve_scoped_calls();
        resolve_free_calls();
        resolve_pending_calls();
        check_exception_types();

        // Everything was found in processing order; report it in source order.
        const auto by_first_use = [](const auto& a, const auto& b) { return before(a.first_use, b.first_use); };
        std::stable_sort(classes_.begin(), classes_.end(), by_first_use);
        std::stable_sort(functions_.begin(), functions_.end(), by_first_use);

        Classified out;
        out.facts = std::move(facts_);
        out.classes = std::move(classes_);
        out.functions = std::move(functions_);
        return out;
    }

private:
    // ---- what is already provided -------------------------------------------------------------------

    void index_environment() {
        for (const ir::Class& c : facts_.local.classes) {
            local_classes_.insert(c.name);
            for (const ir::Method& m : c.methods) methods_[c.name].insert(m.name);
            for (const ir::Field& f : c.fields) methods_[c.name].insert(f.name);
            for (const ir::BaseSpec& b : c.bases) bases_[c.name].push_back(parse_type_name(b.name).name);
            add_namespaces(c.ns);
        }
        for (const ir::Enum& e : facts_.local.enums) {
            local_types_.insert(e.name);
            add_namespaces(e.ns);
        }
        for (const ir::Function& f : facts_.local.functions) {
            local_functions_.push_back({f.ns, f.name});
            add_namespaces(f.ns);
        }
        for (const std::string& n : facts_.local_type_names) local_types_.insert(n);

        for (const ir::Model& m : options_.known) {
            for (const ir::Class& c : m.classes) {
                known_types_.push_back({c.ns, c.name});
                for (const ir::Method& method : c.methods) methods_[c.name].insert(method.name);
                for (const ir::Field& f : c.fields) methods_[c.name].insert(f.name);
                for (const ir::BaseSpec& b : c.bases) bases_[c.name].push_back(parse_type_name(b.name).name);
            }
            for (const ir::Enum& e : m.enums) known_types_.push_back({e.ns, e.name});
            for (const ir::Function& f : m.functions) known_functions_.push_back({f.ns, f.name});
        }

        for (const std::string& u : facts_.using_namespaces) {
            const Ns parts = split_scope(u);
            if (parts.empty()) continue;
            add_namespaces(parts);
            if (join(parts) == "std") using_std_ = true;
            if (is_foreign_namespace(parts.front())) continue;
            if (std::find(usings_.begin(), usings_.end(), parts) == usings_.end()) usings_.push_back(parts);
        }
        for (const TypeName& u : facts_.using_declarations) {
            add_namespaces(u.scope);
            // `using std::string;` brings the name in: from then on it is std's.
            if (!u.scope.empty() && is_foreign_namespace(u.scope.front())) using_foreign_.insert(u.name);
        }

        for (const TestCaseInfo& t : facts_.tests) add_namespaces(t.ns);
        for (const TypeUse& t : facts_.type_uses) add_namespaces(t.type.scope);
        for (const Construction& c : facts_.constructions) add_namespaces(c.type.scope);
        for (const MethodCall& m : facts_.method_calls) add_namespaces(m.receiver.scope);
        for (const FieldAccess& f : facts_.field_accesses) add_namespaces(f.receiver.scope);

        default_ns_ = split_scope(options_.config.default_namespace);
    }

    void add_namespaces(const Ns& parts) {
        for (const std::string& p : parts) ns_names_.insert(p);
    }

    bool is_known_type(const TypeName& t) const {
        for (const KnownType& k : known_types_) {
            if (k.name == t.name && (t.scope.empty() || ends_with(k.ns, t.scope))) return true;
        }
        return false;
    }

    bool is_known_class_name(const std::string& name) const {
        for (const KnownType& k : known_types_) {
            if (k.name == name) return true;
        }
        return false;
    }

    bool configured_external_type(const TypeName& t) const {
        const std::string written = t.qualified();
        for (const std::string& raw : options_.config.external_types) {
            const std::string entry = join(split_scope(raw));
            if (entry == written || entry == t.name) return true;
            if (entry.size() > written.size() && entry.compare(entry.size() - written.size() - 2, std::string::npos, "::" + written) == 0) {
                return true;
            }
        }
        return false;
    }

    // Never generated: the language, the libraries and frameworks, the file
    // itself, the headers it includes, the configuration (design 7.7).
    bool is_external_type(const TypeName& t) const {
        if (!t.scope.empty() && is_foreign_namespace(t.scope.front())) return true;
        if (t.scope.empty()) {
            if (is_qt_class(t.name) || is_standard_c_type(t.name) || using_foreign_.count(t.name) != 0) return true;
            if (using_std_ && is_std_type(t.name)) return true;
            if (local_classes_.count(t.name) != 0 || local_types_.count(t.name) != 0) return true;
        } else {
            // Outer::Inner where Outer is not ours: a nested type of somebody else's class.
            const std::string& outer = t.scope.back();
            if (local_classes_.count(outer) != 0 || is_known_class_name(outer) || is_qt_class(outer)) return true;
        }
        return is_known_type(t) || configured_external_type(t);
    }

    bool is_known_function(const Ns& ns, const std::string& name) const {
        const auto matches = [&](const std::vector<QualifiedName>& list) {
            for (const QualifiedName& f : list) {
                if (f.name == name && (ns.empty() || ends_with(f.ns, ns))) return true;
            }
            return false;
        };
        return matches(local_functions_) || matches(known_functions_);
    }

    bool configured_external_function(const std::string& name, const Ns& ns) const {
        const std::string qualified = join(ns) + (ns.empty() ? "" : "::") + name;
        for (const std::string& raw : options_.config.external_functions) {
            const std::string entry = join(split_scope(raw));
            if (entry == qualified || entry == name) return true;
            // `vendor::init` also covers a bare init() under `using namespace vendor;`.
            if (entry.size() > qualified.size() && entry.compare(entry.size() - qualified.size() - 2, std::string::npos, "::" + qualified) == 0) {
                return true;
            }
        }
        return false;
    }

    bool owner_has_method(const std::string& owner, const std::string& name) const {
        std::set<std::string> seen;
        std::vector<std::string> pending{owner};
        while (!pending.empty()) {
            const std::string current = pending.back();
            pending.pop_back();
            if (current.empty() || !seen.insert(current).second) continue;
            const auto methods = methods_.find(current);
            if (methods != methods_.end() && methods->second.count(name) != 0) return true;
            const auto bases = bases_.find(current);
            if (bases != bases_.end()) pending.insert(pending.end(), bases->second.begin(), bases->second.end());
        }
        return false;
    }

    // ---- where things go ------------------------------------------------------------------------------

    // The namespace of an entity the test does not qualify: the annotation, the
    // namespace the test sits in, the one `using namespace`, the configuration.
    Ns resolve_namespace(const std::string& entity, const Ns& test_ns, const ir::SourceLoc& where, bool is_class) {
        if (is_class) {
            const auto annotated = options_.class_namespace.find(entity);
            if (annotated != options_.class_namespace.end()) return split_scope(annotated->second);
        }
        if (!test_ns.empty()) return test_ns;
        if (usings_.size() == 1) {
            if (reported_.insert("using:" + entity).second) {
                diag_.note(core::code::ambiguous_scope,
                           "'" + entity + "' is placed in the namespace '" + join(usings_.front()) +
                               "' because the file says 'using namespace " + join(usings_.front()) + ";'",
                           where);
            }
            return usings_.front();
        }
        if (usings_.size() > 1 && default_ns_.empty()) {
            if (reported_.insert("using:" + entity).second) {
                std::string all;
                for (const Ns& u : usings_) all += (all.empty() ? "'" : ", '") + join(u) + "'";
                diag_.warning(core::code::ambiguous_scope,
                              "'" + entity + "' could belong to any of the namespaces " + all +
                                  "; it goes to the global namespace (qualify it, annotate it, or set default_namespace)",
                              where);
            }
            return {};
        }
        return default_ns_;
    }

    std::size_t find_class(const std::string& name, const Ns& ns, bool exact_ns) const {
        for (std::size_t i = 0; i < classes_.size(); ++i) {
            if (classes_[i].name == name && (!exact_ns || classes_[i].ns == ns)) return i;
        }
        return kNone;
    }

    std::size_t add_class(const std::string& name, Ns ns, const ir::SourceLoc& where) {
        ClassTarget c;
        c.name = name;
        c.ns = std::move(ns);
        c.first_use = where;
        classes_.push_back(std::move(c));
        return classes_.size() - 1;
    }

    // The class a mention refers to, creating it on first sight. A qualified
    // mention is that class; an unqualified one is the class of that name that
    // the file qualifies somewhere, or else wherever the context puts it.
    std::size_t class_for(const TypeName& t, const Ns& test_ns, const ir::SourceLoc& where) {
        if (!t.scope.empty()) {
            const std::size_t found = find_class(t.name, t.scope, true);
            return found != kNone ? found : add_class(t.name, t.scope, where);
        }
        const auto qualified = explicit_scopes_.find(t.name);
        if (qualified != explicit_scopes_.end() && qualified->second.size() > 1 &&
            reported_.insert("twice:" + t.name).second) {
            std::string all;
            for (const Ns& s : qualified->second) all += (all.empty() ? "" : ", ") + join(s) + "::" + t.name;
            diag_.warning(core::code::ambiguous_scope,
                          "'" + t.name + "' is written as " + all + "; the uses without a qualifier are taken as " +
                              join(qualified->second.front()) + "::" + t.name,
                          where);
        }

        const std::size_t found = find_class(t.name, {}, false);
        if (found != kNone) return found;
        if (qualified != explicit_scopes_.end()) return add_class(t.name, qualified->second.front(), where);
        return add_class(t.name, resolve_namespace(t.name, test_ns, where, true), where);
    }

    // ---- classes from types ---------------------------------------------------------------------------------

    void collect_classes() {
        std::vector<Occurrence> occurrences;
        const auto add = [&](Occurrence::Kind kind, std::size_t index, const TypeName& type, const Ns& ns,
                             const ir::SourceLoc& where) {
            occurrences.push_back({kind, index, type, ns, where});
        };
        for (std::size_t i = 0; i < facts_.type_uses.size(); ++i) {
            const TypeUse& u = facts_.type_uses[i];
            if (!u.from_throw_assertion) add(Occurrence::Kind::Type, i, u.type, u.ns, u.where);
        }
        for (std::size_t i = 0; i < facts_.constructions.size(); ++i) {
            const Construction& c = facts_.constructions[i];
            add(Occurrence::Kind::Construction, i, c.type, c.ns, c.where);
        }
        for (std::size_t i = 0; i < facts_.method_calls.size(); ++i) {
            const MethodCall& m = facts_.method_calls[i];
            add(Occurrence::Kind::Method, i, m.receiver, m.ns, m.where);
        }
        for (std::size_t i = 0; i < facts_.field_accesses.size(); ++i) {
            const FieldAccess& f = facts_.field_accesses[i];
            add(Occurrence::Kind::Field, i, f.receiver, f.ns, f.where);
        }
        std::stable_sort(occurrences.begin(), occurrences.end(),
                         [](const Occurrence& a, const Occurrence& b) { return before(a.where, b.where); });

        // Names written without a scope that nobody provides: the classes. A
        // qualified name whose qualifier is one of them is a nested type.
        std::set<std::string> plain;
        for (const Occurrence& o : occurrences) {
            if (o.type.scope.empty() && !is_external_type(o.type)) plain.insert(o.type.name);
        }
        const auto nested = [&](const Occurrence& o) {
            return !o.type.scope.empty() && plain.count(o.type.scope.back()) != 0;
        };

        // Where the file says each class lives: `using acme::Foo;` first, then
        // every qualified mention, in source order.
        for (const TypeName& u : facts_.using_declarations) {
            if (!u.scope.empty() && !is_external_type(u)) add_explicit_scope(u.name, u.scope);
        }
        for (const Occurrence& o : occurrences) {
            if (!o.type.scope.empty() && !is_external_type(o.type) && !nested(o)) add_explicit_scope(o.type.name, o.type.scope);
        }

        for (const Occurrence& o : occurrences) {
            if (is_external_type(o.type)) continue;
            if (nested(o)) {
                if (reported_.insert("nested:" + o.type.qualified()).second) {
                    diag_.warning(core::code::unsupported,
                                  "'" + o.type.qualified() + "' is a type nested in '" + o.type.scope.back() +
                                      "', which will be generated; nested types are not generated",
                                  o.where);
                }
                continue;
            }
            attach(class_for(o.type, o.test_ns, o.where), o);
        }
    }

    void add_explicit_scope(const std::string& name, const Ns& scope) {
        std::vector<Ns>& scopes = explicit_scopes_[name];
        if (std::find(scopes.begin(), scopes.end(), scope) == scopes.end()) scopes.push_back(scope);
    }

    void attach(std::size_t ci, const Occurrence& o) {
        ClassTarget& c = classes_[ci];
        note_first_use(c.first_use, o.where);
        switch (o.kind) {
            case Occurrence::Kind::Type:         break;
            case Occurrence::Kind::Construction: c.constructions.push_back(o.index); break;
            case Occurrence::Kind::Method:       c.method_calls.push_back(o.index); break;
            case Occurrence::Kind::Field:        c.field_accesses.push_back(o.index); break;
        }
        const std::string& args = o.type.template_args;
        if (!args.empty() && std::find(c.instantiations.begin(), c.instantiations.end(), args) == c.instantiations.end()) {
            c.instantiations.push_back(args);
        }
    }

    // ---- X::m(): a class or a namespace (R13) ------------------------------------------------------------------------

    std::size_t find_function(const Ns& ns, const std::string& name, bool exact_ns) const {
        for (std::size_t i = 0; i < functions_.size(); ++i) {
            if (functions_[i].name == name && (!exact_ns || functions_[i].ns == ns)) return i;
        }
        return kNone;
    }

    std::size_t function_for(const Ns& ns, const std::string& name, const ir::SourceLoc& where) {
        const std::size_t found = find_function(ns, name, true);
        if (found != kNone) return found;
        FunctionTarget f;
        f.ns = ns;
        f.name = name;
        f.first_use = where;
        functions_.push_back(std::move(f));
        return functions_.size() - 1;
    }

    void resolve_scoped_calls() {
        for (std::size_t i = 0; i < facts_.scoped_calls.size(); ++i) {
            const ScopedCall call = facts_.scoped_calls[i];
            if (call.scope.empty()) continue;
            const std::string& x = call.scope.back();
            const Ns outer(call.scope.begin(), call.scope.end() - 1);

            if (is_foreign_namespace(call.scope.front())) continue;                       // std::move, testing::Return
            if (is_external_type(TypeName{outer, x, ""})) continue;                       // QString::number, Fixture::SetUp
            if (is_known_function(call.scope, call.name)) continue;                       // helpers::twice, declared in the file

            // 1. a class that is, or must be, generated: a static method.
            const std::size_t known_class = find_class(x, outer, !outer.empty());
            if (known_class != kNone) {
                add_static(known_class, i, call);
                continue;
            }
            // 2. a namespace the file shows: a function in it.
            if (ns_names_.count(x) != 0) {
                add_namespaced_function(i, call);
                continue;
            }
            // 3. no way to know: capitalised is a class, else a namespace; say so.
            const bool as_class = starts_upper(x);
            diag_.warning(core::code::ambiguous_scope,
                          "'" + join(call.scope) + "::" + call.name + "' could be a static method of the class '" + x +
                              "' or a function in the namespace '" + join(call.scope) + "'; assuming " +
                              (as_class ? "a class (it starts with a capital)" : "a namespace"),
                          call.where);
            if (as_class) {
                const std::size_t ci = class_for(TypeName{outer, x, ""}, call.ns, call.where);
                add_static(ci, i, call);
            } else {
                add_namespaced_function(i, call);
            }
        }
    }

    void add_static(std::size_t ci, std::size_t call_index, const ScopedCall& call) {
        note_first_use(classes_[ci].first_use, call.where);
        classes_[ci].static_calls.push_back(call_index);
    }

    void add_namespaced_function(std::size_t call_index, const ScopedCall& call) {
        const std::size_t fi = function_for(call.scope, call.name, call.where);
        note_first_use(functions_[fi].first_use, call.where);
        functions_[fi].scoped_calls.push_back(call_index);
        std::vector<Ns>& scopes = function_scopes_[call.name];
        if (std::find(scopes.begin(), scopes.end(), call.scope) == scopes.end()) scopes.push_back(call.scope);
    }

    // ---- f(x): a function, a macro, a library call, or a functional cast (7.8) ------------------------------------------------

    void resolve_free_calls() {
        cast_class_.assign(facts_.free_calls.size(), kNone);
        external_cast_.assign(facts_.free_calls.size(), false);

        for (std::size_t i = 0; i < facts_.free_calls.size(); ++i) {
            const FreeCall call = facts_.free_calls[i];
            const std::string& name = call.name;

            if (contains(options_.config.ignore_macros, name)) continue;

            // Foo(3) where Foo is a class: a construction.
            const std::size_t ci = find_class(name, {}, false);
            if (ci != kNone) {
                Construction c;
                c.type.name = name;
                c.form = Construction::Form::Functional;
                c.args = call.args;
                c.where = call.where;
                c.ns = call.ns;
                facts_.constructions.push_back(std::move(c));
                classes_[ci].constructions.push_back(facts_.constructions.size() - 1);
                note_first_use(classes_[ci].first_use, call.where);
                cast_class_[i] = ci;
                continue;
            }
            if (is_macro_like(name)) continue;
            if (is_external_type(TypeName{{}, name, ""})) {      // QString("x"), size_t(3), Fixture(): a cast of somebody else's type
                external_cast_[i] = true;
                continue;
            }
            if (is_c_function(name) || is_qt_function(name) || is_framework_member(name)) continue;
            if (using_foreign_.count(name) != 0) continue;                  // using std::swap;
            if (using_std_ && is_std_function(name)) continue;
            if (configured_external_function(name, {}) || is_known_function({}, name)) continue;
            if (!call.owner.empty() && owner_has_method(call.owner, name)) continue;

            Ns ns;
            const auto scoped = function_scopes_.find(name);
            if (scoped != function_scopes_.end()) ns = scoped->second.front();   // the file also writes ns::name(...)
            else ns = resolve_namespace(name, call.ns, call.where, false);
            const std::size_t fi = function_for(ns, name, call.where);
            note_first_use(functions_[fi].first_use, call.where);
            functions_[fi].free_calls.push_back(i);
        }
    }

    static bool contains(const std::vector<std::string>& list, const std::string& value) {
        return std::find(list.begin(), list.end(), value) != list.end();
    }

    // ---- foo.make().run() and `auto x = Foo(3); x.run();` --------------------------------------------------------------

    void resolve_pending_calls() {
        for (Unresolved& u : facts_.unresolved) {
            if (u.root.kind != FactRef::Kind::FreeCall) continue;
            if (external_cast_[u.root.index]) {   // QString("x").size(): somebody else's class
                u.resolved = true;
                u.method_call = kNone;
                continue;
            }
            const std::size_t ci = cast_class_[u.root.index];
            if (ci == kNone) continue;

            MethodCall m = u.call;
            m.receiver.name = facts_.free_calls[u.root.index].name;
            facts_.method_calls.push_back(std::move(m));
            classes_[ci].method_calls.push_back(facts_.method_calls.size() - 1);
            u.resolved = true;
            u.method_call = facts_.method_calls.size() - 1;
        }
    }

    // ---- exception types -------------------------------------------------------------------------------------------------

    // EXPECT_THROW(stmt, E): E is not a class to generate by itself -- it would
    // need a base class the test does not state -- so it must come from somewhere.
    void check_exception_types() {
        for (const TypeUse& u : facts_.type_uses) {
            if (!u.from_throw_assertion || is_external_type(u.type)) continue;
            if (find_class(u.type.name, u.type.scope, !u.type.scope.empty()) != kNone) continue;
            diag_.warning(core::code::unknown_type,
                          "the exception type '" + u.type.qualified() + "' is neither known nor generated; "
                          "declare it in a header the test includes (an exception class needs a base the test does not say)",
                          u.where);
        }
    }

    struct QualifiedName {
        Ns          ns;
        std::string name;
    };

    TestFileFacts          facts_;
    const ClassifyOptions& options_;
    core::Diagnostics&     diag_;

    std::set<std::string>                          local_classes_;
    std::set<std::string>                          local_types_;
    std::vector<QualifiedName>                     local_functions_;
    std::vector<QualifiedName>                     known_functions_;
    std::vector<KnownType>                         known_types_;
    std::map<std::string, std::set<std::string>>   methods_;
    std::map<std::string, std::vector<std::string>> bases_;
    std::vector<Ns>                                usings_;
    bool                                           using_std_ = false;
    std::set<std::string>                          ns_names_;
    std::set<std::string>                          using_foreign_;       // names `using std::x;` brought in
    Ns                                             default_ns_;

    std::map<std::string, std::vector<Ns>>         explicit_scopes_;     // class name -> where the file qualifies it
    std::map<std::string, std::vector<Ns>>         function_scopes_;     // function name -> where the file qualifies it
    std::set<std::string>                          reported_;

    std::vector<ClassTarget>                       classes_;
    std::vector<FunctionTarget>                    functions_;
    std::vector<std::size_t>                       cast_class_;          // per free call: the class it constructs
    std::vector<bool>                              external_cast_;       // per free call: a cast to somebody else's type
};

}  // namespace

Classified classify(TestFileFacts facts, const ClassifyOptions& options, core::Diagnostics& diagnostics) {
    return Classifier(std::move(facts), options, diagnostics).run();
}

}  // namespace mt::frontend
