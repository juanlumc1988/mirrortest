// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/frontend/code_frontend.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>

#include "mirrortest/core/naming.h"
#include "mirrortest/ir/type.h"
#include "mirrortest/syntax/extract.h"

namespace mt::frontend {

namespace {

using syntax::Declarator;
using syntax::Node;
using syntax::QtMarker;
using syntax::Specifiers;

constexpr unsigned kMaxParseWarnings = 10;
constexpr std::size_t kDirectiveReach = 4096;

bool is_class_like(std::string_view type) {
    return type == "class_specifier" || type == "struct_specifier" || type == "union_specifier";
}

bool is_conditional(std::string_view type) {
    return type == "preproc_if" || type == "preproc_ifdef" || type == "preproc_else" ||
           type == "preproc_elif" || type == "preproc_elifdef";
}

// Whether [begin, end) lies inside one preprocessor directive, with its continuation lines: `#elif HAS_INCLUDE(<any>)` is
// no C++ to the parser, but it hides no declaration, because every branch is read whatever the condition says (design 6.5).
// A directive is looked for within kDirectiveReach bytes, so that a file of one very long line is not a quadratic walk.
bool within_directive(std::string_view text, std::size_t begin, std::size_t end) {
    auto line_break = [&](std::size_t at) { return text[at] == '\n' && (at == 0 || text[at - 1] != '\\'); };
    std::size_t start = begin;
    while (start > 0 && begin - start < kDirectiveReach && !line_break(start - 1)) --start;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) ++start;
    if (start >= text.size() || text[start] != '#') return false;
    std::size_t stop = begin;
    while (stop < text.size() && stop - begin < kDirectiveReach && !line_break(stop)) ++stop;
    return end <= stop;
}

// The grammar reads `int S::*p` and `int (S::*f)(int)` as a parameter called `S::*p` of type `int`: there is no declaration
// to make a test from, and no way to say it in a stub. (libclang reads them.)
bool has_pointer_to_member_parameter(const std::vector<ir::Param>& params) {
    return std::any_of(params.begin(), params.end(), [](const ir::Param& p) {
        return p.name.find_first_of(":*&") != std::string::npos;
    });
}

// What a member or a function can be called: an identifier, `~Identifier`, `operator…`. Anything else (`std::declval<A>`,
// nothing at all) is what the parser made of text it could not read, which `report_parse_errors` has already said, and
// would become a test of a member that does not exist.
bool is_declarator_name(const std::string& name) {
    if (ir::is_operator_name(name)) return true;
    std::size_t i = !name.empty() && name[0] == '~' ? 1 : 0;
    if (i >= name.size() || std::isdigit(static_cast<unsigned char>(name[i])) != 0) return false;
    for (; i < name.size(); ++i) {
        const char c = name[i];
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') return false;
    }
    return true;
}

ir::Access to_access(std::string_view text) {
    if (text == "public") return ir::Access::Public;
    if (text == "protected") return ir::Access::Protected;
    return ir::Access::Private;
}

// Two methods are the same declaration if nothing but the parameter names and
// the return type differs. Used to merge a class read twice (both branches of
// an #if) without listing a member twice.
std::string signature(const ir::Method& m) {
    std::string sig = m.name + "(";
    for (const ir::Param& p : m.params) {
        sig += p.type.spelling + ",";
    }
    sig += m.is_const ? ")const" : ")";
    if (m.ref_qualifier == ir::RefKind::LValue) sig += "&";
    if (m.ref_qualifier == ir::RefKind::RValue) sig += "&&";
    return sig;
}

class Reader {
public:
    Reader(const syntax::Tree& tree, const std::vector<QtMarker>& markers, const std::string& file,
           const std::string& header, ir::Model& model, core::Diagnostics& diagnostics, bool anonymous_namespaces)
        : tree_(tree), markers_(markers), file_(file), header_(header), model_(model),
          diagnostics_(diagnostics), anonymous_namespaces_(anonymous_namespaces) {}

    void run() {
        report_parse_errors();
        declarations(tree_.root());
    }

private:
    // State while walking one class body.
    struct ClassState {
        ir::Class&  cls;
        ir::Access  access;
        ir::QtRole  section = ir::QtRole::None;
        unsigned    previous_end = 0;
        std::string key;          // the qualified name, for the notes about #if branches
    };

    // One branch of an #if the walk is inside of: the conditional (by where it
    // starts) and the branch's number in it (if = 0, elif/else = 1, 2, ...).
    struct Branch {
        unsigned    chain = 0;
        unsigned    index = 0;
        std::string condition;                // of this branch, as the text of an #if; empty for an #else
        std::vector<std::string> earlier;     // the conditions of the branches before it in its conditional
        bool        include_guard = false;    // `#ifndef X_H` / `#define X_H` around the file: a condition of no test
    };

    // A method as seen in the walk, with the branches it was declared under, to
    // notice two declarations that are alternatives of each other.
    struct Declared {
        std::string         name;
        std::size_t         arity = 0;
        std::string         signature;
        std::string         shape;      // what else can differ: the return type, the flags
        std::string         text;       // as a person says it
        ir::SourceLoc       where;
        std::vector<Branch> branches;
    };

    // ---- diagnostics ----------------------------------------------------------------

    void report_parse_errors() {
        unsigned reported = 0;
        unsigned more = 0;
        syntax::walk(tree_.root(), [&](const Node& n) {
            if (!n.is_error()) {
                return true;
            }
            if (within_directive(tree_.source(), n.start_byte(), n.end_byte())) {
                return false;
            }
            // QFETCH(int, v) is no C++ to a parser, and the test frontend reads it by text.
            if (syntax::parse_qfetch(n.text())) {
                return false;
            }
            if (reported < kMaxParseWarnings) {
                ++reported;
                // A call such as `MACRO(LT, <)` at namespace scope makes the parser give up on everything after it, and
                // what is inside such an error is not read: the generic text would understate it.
                const bool most_of_the_file = 2 * (n.end_byte() - n.start_byte()) >= tree_.root().end_byte();
                diagnostics_.warning(core::code::parse,
                                     most_of_the_file
                                         ? "the parser lost the structure of most of this file from here on, and the "
                                           "declarations in it are not read; a macro of the project whose arguments are "
                                           "not C++ is the usual cause (list it in ignore_macros)"
                                         : "the parser could not make sense of this text; declarations "
                                           "around it may be missing (a macro of the project? list it in ignore_macros)",
                                     syntax::location(n, file_));
            } else {
                ++more;
            }
            return false;
        });
        if (more > 0) {
            diagnostics_.note(core::code::parse, "and " + std::to_string(more) + " more parse errors");
        }
    }

    // ---- scopes ---------------------------------------------------------------------

    void declarations(const Node& scope) {
        for (const Node& child : scope.named_children()) {
            declaration(child);
        }
    }

    void declaration(const Node& node) {
        const std::string_view type = node.type();
        if (type == "namespace_definition") {
            namespace_definition(node);
        } else if (type == "linkage_specification") {
            const Node body = node.child_by_field("body");
            if (body.type() == "declaration_list") {
                declarations(body);
            } else if (!body.is_null()) {
                declaration(body);
            }
        } else if (is_conditional(type)) {
            // Every branch: the frontend cannot know which one builds.
            enter_branch(node);
            declarations(node);
            branches_.pop_back();
        } else if (type == "preproc_include") {
            include(node);
        } else if (is_class_like(type)) {
            if (type != "union_specifier") {
                class_definition(node, std::nullopt);
            }
        } else if (type == "enum_specifier") {
            enum_definition(node);
        } else if (type == "template_declaration") {
            template_declaration(node);
        } else if (type == "declaration" || type == "function_definition") {
            // `struct S { ... } s;` defines S as well as declaring s.
            const Node specifier = node.child_by_field("type");
            if (!specifier.is_null() && (is_class_like(specifier.type()) || specifier.type() == "enum_specifier")) {
                declaration(specifier);
            }
            free_functions(node);
        }
    }

    void namespace_definition(const Node& node) {
        const Node name = node.child_by_field("name");
        if (name.is_null()) {
            // Internal linkage: unreachable from a test of a header, but the file's own helpers and
            // doubles, when the file is a test.
            if (anonymous_namespaces_) declarations(node.child_by_field("body"));
            return;
        }
        std::vector<std::string> parts;
        std::string_view rest = name.text();
        for (std::size_t sep; (sep = rest.find("::")) != std::string_view::npos;) {
            parts.emplace_back(rest.substr(0, sep));
            rest.remove_prefix(sep + 2);
        }
        parts.emplace_back(rest);

        for (const std::string& p : parts) ns_.push_back(p);
        declarations(node.child_by_field("body"));
        ns_.resize(ns_.size() - parts.size());
    }

    void include(const Node& node) {
        const Node path = node.child_by_field("path");
        std::string text(path.text());
        if (text.size() < 2) {
            return;
        }
        ir::Include inc;
        inc.is_system = text.front() == '<';
        inc.path = text.substr(1, text.size() - 2);
        inc.where = syntax::location(node, file_);
        model_.includes.push_back(std::move(inc));
    }

    void template_declaration(const Node& node) {
        std::string params(node.named_child(0).text());
        if (params.size() >= 2 && params.front() == '<' && params.back() == '>') {
            params = ir::collapse_spaces(std::string_view(params).substr(1, params.size() - 2));
        }
        for (const Node& child : node.named_children()) {
            const std::string_view type = child.type();
            if (type == "concept_definition") {
                unsupported("concept '" + std::string(child.child_by_field("name").text()) + "' is not supported and is skipped", child);
            } else if (type == "requires_clause") {
                unsupported("the requires-clause '" + std::string(child.text()) + "' is ignored", child);
            } else if (type == "class_specifier" || type == "struct_specifier") {
                const Node name = child.child_by_field("name");
                if (!name.is_null() && name.type() == "template_type") {
                    unsupported("the specialisation '" + std::string(name.text()) + "' is not supported and is skipped", name);
                    continue;
                }
                class_definition(child, params);
            } else if (type == "declaration" || type == "function_definition") {
                const Declarator d = syntax::parse_declarator(child.child_by_field("declarator"));
                if (!d.function.is_null() && !d.is_function_pointer) {
                    diagnostics_.warning(core::code::unsupported,
                                         "function template '" + d.name + "' is not supported and is skipped",
                                         syntax::location(d.name_node, file_));
                }
            }
        }
    }

    // ---- enums ----------------------------------------------------------------------

    void enum_definition(const Node& spec) {
        const Node name = spec.child_by_field("name");
        const Node body = spec.child_by_field("body");
        if (name.is_null() || body.is_null()) {
            return;   // anonymous, or a forward declaration
        }
        ir::Enum e;
        e.name = std::string(name.text());
        e.ns = ns_;
        e.is_scoped = syntax::has_token(spec, "class") || syntax::has_token(spec, "struct");
        const Node base = spec.child_by_field("base");
        if (!base.is_null()) {
            e.underlying = ir::normalize_spelling(base.text());
        }
        for (const Node& item : body.named_children()) {
            if (item.type() == "enumerator") {
                e.enumerators.emplace_back(item.child_by_field("name").text());
            }
        }
        e.header = header_;
        e.where = syntax::location(name, file_);
        model_.enums.push_back(std::move(e));
    }

    // ---- free functions -----------------------------------------------------------------

    void free_functions(const Node& node) {
        const Specifiers spec = syntax::read_specifiers(node);
        for (const Node& declarator : syntax::children_by_field(node, "declarator")) {
            const Declarator d = syntax::parse_declarator(declarator);
            if (d.function.is_null() || d.is_function_pointer || d.name_node.is_null() ||
                d.name.find("::") != std::string::npos) {
                continue;   // a variable, or `int Foo::bar() {}`, which defines a member
            }
            if (!is_declarator_name(ir::normalize_operator_name(d.name))) continue;
            const syntax::FunctionTraits traits = syntax::read_function_traits(d.function, file_);
            ir::Function f;
            f.name = ir::normalize_operator_name(d.name);
            f.ns = ns_;
            f.ret = traits.has_trailing_return ? traits.trailing_return
                                               : syntax::make_declared_type(spec, d, file_);
            f.params = syntax::read_parameters(d.function.child_by_field("parameters"), file_);
            if (has_pointer_to_member_parameter(f.params)) {
                unsupported("'" + f.name + "' has a pointer-to-member parameter, which this reader cannot read; it is skipped", d.name_node);
                continue;
            }
            f.is_noexcept = traits.is_noexcept;
            f.header = header_;
            f.where = syntax::location(d.name_node, file_);
            f.guards = inner_guards();
            model_.functions.push_back(std::move(f));
        }
    }

    // ---- classes ----------------------------------------------------------------------------

    ir::Class& find_or_add_class(const std::string& name) {
        for (ir::Class& c : model_.classes) {
            if (c.name == name && c.ns == ns_) {
                return c;
            }
        }
        model_.classes.emplace_back();
        model_.classes.back().name = name;
        model_.classes.back().ns = ns_;
        return model_.classes.back();
    }

    void class_definition(const Node& spec, const std::optional<std::string>& template_params) {
        const Node name = spec.child_by_field("name");
        const Node body = spec.child_by_field("body");
        if (name.is_null() || body.is_null()) {
            return;   // anonymous, or a forward declaration
        }
        if (name.text().find("::") != std::string_view::npos) {
            // `struct Outer::Inner { ... };`: a class defined outside the one that declares it. Read as a class called
            // 'Outer::Inner' it would get tests that do not compile.
            unsupported("the class '" + std::string(name.text()) + "' is defined outside the class that declares it, which is not supported; it is skipped", name);
            return;
        }

        ir::Class& cls = find_or_add_class(std::string(name.text()));
        read_class(cls, spec, body, template_params, ir::qualified_name(ns_, cls.name));
    }

    // The body of a class, into `cls`: whether it is declared at namespace scope or inside another class.
    void read_class(ir::Class& cls, const Node& spec, const Node& body, const std::optional<std::string>& template_params,
                    const std::string& key) {
        const bool first_time = !cls.where.known();   // not yet read: no position recorded
        if (first_time) {
            cls.is_struct = spec.type() == "struct_specifier";
            cls.header = header_;
            cls.origin = ir::Origin::Declared;
            cls.where = syntax::location(spec.child_by_field("name"), file_);
            cls.template_params = template_params;
            read_bases(spec, cls);
        }

        if (first_time) cls.guards = inner_guards();
        const std::size_t outer_base = guard_base_;
        guard_base_ = guard_levels().size();

        ClassState state{cls, cls.is_struct ? ir::Access::Public : ir::Access::Private, ir::QtRole::None, body.start_byte(), key};
        members(body, state);
        read_qt_class_markers(spec, body, cls);
        guard_base_ = outer_base;
    }

    // `struct Item { ... };` inside a class.
    void nested_class(const Node& spec, ClassState& outer) {
        const Node name = spec.child_by_field("name");
        const Node body = spec.child_by_field("body");
        if (name.is_null() || body.is_null() || spec.type() == "union_specifier") return;
        const std::string simple(name.text());
        ir::Class* nested = nullptr;
        for (ir::Class& c : outer.cls.nested_classes) {
            if (c.name == simple) nested = &c;
        }
        if (nested == nullptr) {
            outer.cls.nested_classes.emplace_back();
            nested = &outer.cls.nested_classes.back();
            nested->name = simple;
            nested->ns = outer.cls.ns;
            nested->nested_access = outer.access;
        }
        read_class(*nested, spec, body, std::nullopt, outer.key + "::" + simple);
    }

    void read_bases(const Node& spec, ir::Class& cls) {
        Node clause;
        for (const Node& child : spec.named_children()) {
            if (child.type() == "base_class_clause") clause = child;
        }
        if (clause.is_null()) {
            return;
        }
        std::optional<ir::Access> access;
        bool is_virtual = false;
        for (unsigned i = 0; i < clause.child_count(); ++i) {
            const Node child = clause.child(i);
            const std::string_view type = child.type();
            if (type == "access_specifier") {
                access = to_access(child.text());
            } else if (!child.is_named() && type == "virtual") {
                is_virtual = true;
            } else if (child.is_named() && type != "comment" && type != "attribute_declaration") {
                ir::BaseSpec base;
                base.name = ir::normalize_spelling(child.text());
                base.access = access.value_or(cls.is_struct ? ir::Access::Public : ir::Access::Private);
                base.is_virtual = is_virtual;
                cls.bases.push_back(std::move(base));
                access.reset();
                is_virtual = false;
            }
        }
    }

    // ---- Qt ------------------------------------------------------------------------------------

    bool marker_between(QtMarker::Kind kind, unsigned from, unsigned to) const {
        for (const QtMarker& m : markers_) {
            if (m.kind == kind && m.offset >= from && m.offset < to) return true;
        }
        return false;
    }

    ir::QtRole section_role(unsigned access_specifier_offset) const {
        for (const QtMarker& m : markers_) {
            if (m.offset != access_specifier_offset) continue;
            if (m.kind == QtMarker::Kind::SlotsSection) return ir::QtRole::Slot;
            if (m.kind == QtMarker::Kind::SignalsSection) return ir::QtRole::Signal;
        }
        return ir::QtRole::None;
    }

    // The class-level macros that sit directly in this class, not in a class
    // nested inside it.
    void read_qt_class_markers(const Node& spec, const Node& body, ir::Class& cls) {
        std::vector<std::pair<unsigned, unsigned>> nested;
        for (const char* type : {"class_specifier", "struct_specifier", "union_specifier"}) {
            for (const Node& n : syntax::find_all(body, type)) {
                nested.emplace_back(n.start_byte(), n.end_byte());
            }
        }
        const auto direct = [&](unsigned offset) {
            return std::none_of(nested.begin(), nested.end(), [offset](const std::pair<unsigned, unsigned>& r) {
                return offset >= r.first && offset < r.second;
            });
        };
        for (const QtMarker& m : markers_) {
            if (m.offset < spec.start_byte() || m.offset >= spec.end_byte() || !direct(m.offset)) continue;
            if (m.kind == QtMarker::Kind::QObject) cls.is_qobject = true;
            if (m.kind == QtMarker::Kind::Gadget) cls.is_gadget = true;
            if (m.kind == QtMarker::Kind::DisableCopy) cls.disable_copy = true;
        }
    }

    void unsupported(const std::string& message, const Node& at) {
        diagnostics_.warning(core::code::unsupported, message, syntax::location(at, file_));
    }

    // `using Base::Base;` makes Base's constructors the derived class's own.
    void inherited_constructors(const Node& using_declaration) {
        std::string text(using_declaration.text());
        if (text.compare(0, 5, "using") != 0) return;
        text.erase(0, 5);
        while (!text.empty() && (text.back() == ';' || text.back() == ' ')) text.pop_back();
        const std::size_t last = text.rfind("::");
        if (last == std::string::npos) return;
        const std::size_t before = text.rfind("::", last == 0 ? 0 : last - 1);
        const std::string tail = text.substr(last + 2);
        const std::string scope = text.substr(before == std::string::npos ? 0 : before + 2, last - (before == std::string::npos ? 0 : before + 2));
        const auto trim = [](std::string s) {
            while (!s.empty() && s.front() == ' ') s.erase(0, 1);
            while (!s.empty() && s.back() == ' ') s.pop_back();
            return s;
        };
        if (!trim(tail).empty() && trim(tail) == trim(scope)) {
            unsupported("'using" + text + ";' inherits constructors, which are not supported", using_declaration);
        }
    }

    // ---- conditional compilation -----------------------------------------------------------------

    // `preproc_if` starts a conditional; its `preproc_elif` / `preproc_else` are its
    // children and continue the one it is inside of with the next branch number.
    void enter_branch(const Node& node) {
        const std::string_view type = node.type();
        const bool continues = type == "preproc_else" || type == "preproc_elif" || type == "preproc_elifdef";
        Branch b;
        if (continues && !branches_.empty()) {
            b.chain = branches_.back().chain;
            b.index = branches_.back().index + 1;
            b.earlier = branches_.back().earlier;
            b.earlier.push_back(branches_.back().condition);
        } else {
            b.chain = node.start_byte();
        }
        b.condition = branch_condition(node);
        b.include_guard = !continues && is_include_guard(node);
        branches_.push_back(std::move(b));
    }

    // The text of the `#if` that is true when the branch is taken: the condition of an `#if` and an `#elif` as written (a
    // line continued with a backslash is one line), `defined(NAME)` for `#ifdef` and `!defined(NAME)` for `#ifndef`.
    static std::string branch_condition(const Node& node) {
        const std::string_view type = node.type();
        if (type == "preproc_else") return std::string();
        if (type == "preproc_if" || type == "preproc_elif") {
            std::string text(node.child_by_field("condition").text());
            for (std::size_t at; (at = text.find("\\\n")) != std::string::npos;) text.replace(at, 2, " ");
            return ir::collapse_spaces(text);
        }
        const bool negated = node.child(0).text().find("ndef") != std::string_view::npos;   // #ifndef, #elifndef
        return std::string(negated ? "!defined(" : "defined(") + std::string(node.child_by_field("name").text()) + ")";
    }

    // `#ifndef NAME` whose first line inside is `#define NAME`: the guard of a header against being included twice. The
    // tests are written after the header is included, when NAME is defined, so a test under it would never be compiled.
    static bool is_include_guard(const Node& node) {
        if (node.type() != "preproc_ifdef" || node.child(0).text().find("ndef") == std::string_view::npos) return false;
        const Node name = node.child_by_field("name");
        for (const Node& child : node.named_children()) {
            if (child == name || child.type() == "comment") continue;
            return child.type() == "preproc_def" && child.child_by_field("name").text() == name.text();
        }
        return false;
    }

    // The conditions of the `#if` branches the walk is inside of, outermost first, each as the text of an `#if`: a branch
    // after the first of its conditional is under the negation of the ones before it (`!(A) && (B)` for the `#elif B` of
    // `#if A`; `!(A) && !(B)` for the `#else`). An entry that has a continuation is not a condition on its own: the
    // continuation stands for the branch taken.
    std::vector<std::string> guard_levels() const {
        std::vector<std::string> levels;
        for (std::size_t i = 0; i < branches_.size(); ++i) {
            const Branch& b = branches_[i];
            if (b.include_guard || (i + 1 < branches_.size() && branches_[i + 1].chain == b.chain)) continue;
            std::string guard;
            for (const std::string& before : b.earlier) guard += (guard.empty() ? "" : " && ") + std::string("!(") + before + ")";
            if (!b.condition.empty()) {
                guard += (guard.empty() ? "" : " && ") + (b.earlier.empty() ? b.condition : "(" + b.condition + ")");
            }
            levels.push_back(std::move(guard));
        }
        return levels;
    }

    // Those of guard_levels() that are inside the class being read (the ones before are the class's own).
    std::vector<std::string> inner_guards() const {
        std::vector<std::string> levels = guard_levels();
        levels.erase(levels.begin(), levels.begin() + static_cast<std::ptrdiff_t>(std::min(guard_base_, levels.size())));
        return levels;
    }

    static bool alternatives(const std::vector<Branch>& a, const std::vector<Branch>& b) {
        for (const Branch& x : a) {
            for (const Branch& y : b) {
                if (x.chain == y.chain && x.index != y.index) return true;
            }
        }
        return false;
    }

    Declared describe(const ir::Method& m) const {
        Declared d;
        d.name = m.name;
        d.arity = m.params.size();
        d.signature = signature(m);
        d.shape = m.ret.spelling + (m.is_static ? "|static" : "") + (m.is_virtual ? "|virtual" : "") + (m.is_noexcept ? "|noexcept" : "") +
                  (m.is_explicit ? "|explicit" : "") + (m.is_deleted ? "|deleted" : "") + (m.is_pure ? "|pure" : "");
        std::string params;
        for (const ir::Param& p : m.params) params += (params.empty() ? "" : ", ") + p.type.spelling;
        d.text = (m.ret.empty() ? std::string() : m.ret.spelling + " ") + m.name + "(" + params + ")" + (m.is_const ? " const" : "");
        d.where = m.where;
        d.branches = branches_;
        return d;
    }

    // Two declarations of one member under the alternatives of one #if are not
    // an error, but the user is the one who knows which applies: say so.
    void note_conditional(const ClassState& state, const Declared& first, const Declared& second, bool both_kept) {
        core::Diagnostic d;
        d.severity = core::Severity::Note;
        d.code = core::code::conditional;
        d.where = second.where;
        d.message = "'" + state.key + "::" + second.name + "' is declared differently under the branches of an #if: '" + first.text +
                    "' and '" + second.text + "'; " + (both_kept ? "both are kept" : "the first is kept");
        d.notes.push_back({first.where, "the other declaration"});
        diagnostics_.add(std::move(d));
    }

    // ---- members --------------------------------------------------------------------------------

    void members(const Node& body, ClassState& state) {
        for (const Node& child : body.named_children()) {
            const std::string_view type = child.type();
            if (type == "comment") {
                continue;
            }
            if (type == "access_specifier") {
                state.access = to_access(child.text());
                state.section = section_role(child.start_byte());
            } else if (is_conditional(type)) {
                enter_branch(child);
                members(child, state);
                branches_.pop_back();
                continue;
            } else if (type == "using_declaration") {
                inherited_constructors(child);
            } else if (type == "field_declaration" || type == "declaration" || type == "function_definition") {
                member(child, state, false);
            } else if (type == "template_declaration") {
                for (const Node& inner : child.named_children()) {
                    const std::string_view t = inner.type();
                    if (t == "field_declaration" || t == "declaration" || t == "function_definition") {
                        member(inner, state, true);
                    }
                }
            } else if (type == "type_definition") {
                add_nested(state.cls, std::string(syntax::parse_declarator(child.child_by_field("declarator")).name));
            } else if (type == "alias_declaration") {
                add_nested(state.cls, std::string(child.child_by_field("name").text()));
            }
            // friend, using, static_assert and the rest are not members.
            state.previous_end = child.end_byte();
        }
    }

    static void add_nested(ir::Class& cls, const std::string& name) {
        if (!name.empty() && std::find(cls.nested_types.begin(), cls.nested_types.end(), name) == cls.nested_types.end()) {
            cls.nested_types.push_back(name);
        }
    }

    static bool has_child_of_type(const Node& node, std::string_view type) {
        for (unsigned i = 0; i < node.child_count(); ++i) {
            if (node.child(i).type() == type) return true;
        }
        return false;
    }

    void member(const Node& node, ClassState& state, bool is_template) {
        const Specifiers spec = syntax::read_specifiers(node);
        const std::vector<Node> declarators = syntax::children_by_field(node, "declarator");

        if (declarators.empty()) {
            // `struct N { ... };` and `enum class E { ... };` inside a class.
            const Node type = node.child_by_field("type");
            if (!type.is_null() && (is_class_like(type.type()) || type.type() == "enum_specifier")) {
                const std::string name(type.child_by_field("name").text());
                add_nested(state.cls, name);
                if (is_class_like(type.type())) nested_class(type, state);
                if (type.type() == "enum_specifier" && state.access == ir::Access::Public && !name.empty() &&
                    std::find(state.cls.nested_enums.begin(), state.cls.nested_enums.end(), name) == state.cls.nested_enums.end()) {
                    state.cls.nested_enums.push_back(name);
                }
            }
            return;
        }

        const bool invokable =
            marker_between(QtMarker::Kind::Invokable, state.previous_end, node.start_byte());
        for (const Node& declarator : declarators) {
            if (declarator.type() == "operator_cast") {
                conversion(node, declarator, spec, state, is_template, invokable);
                continue;
            }
            const Declarator d = syntax::parse_declarator(declarator);
            if (d.name_node.is_null() || !is_declarator_name(ir::normalize_operator_name(d.name))) {
                continue;
            }
            if (!d.function.is_null() && !d.is_function_pointer) {
                method(node, spec, d, state, is_template, invokable);
            } else if (d.name == "override" || d.name == "final") {
                // `~X() NOEXCEPT_MACRO override;` is a field called `override` to a parser that does not know the macro.
                diagnostics_.warning(core::code::parse,
                                     "'" + d.name + "' is read as the name of a field, which is most likely a macro of the "
                                     "project in front of the keyword; it is skipped (list the macro in ignore_macros)",
                                     syntax::location(d.name_node, file_));
            } else {
                field(spec, d, state);
            }
        }
    }

    ir::QtRole role_for(const ClassState& state, bool invokable) const {
        if (state.section != ir::QtRole::None) return state.section;
        return invokable ? ir::QtRole::Invokable : ir::QtRole::None;
    }

    void add_method(ClassState& state, ir::Method m) {
        const Declared now = describe(m);
        std::vector<Declared>& seen = declared_[state.key];
        for (const Declared& before : seen) {
            if (before.name != now.name || before.arity != now.arity || !alternatives(before.branches, now.branches)) continue;
            if (before.signature != now.signature) {
                note_conditional(state, before, now, true);
            } else if (before.shape != now.shape) {
                note_conditional(state, before, now, false);
            }
        }
        m.guards = inner_guards();
        for (ir::Method& existing : state.cls.methods) {
            if (signature(existing) == now.signature) {
                // Declared in two branches: it is there whenever either is taken, which is not a condition to write.
                if (existing.guards != m.guards) existing.guards.clear();
                return;
            }
        }
        seen.push_back(now);
        state.cls.methods.push_back(std::move(m));
    }

    void apply_declaration_flags(const Node& node, const Specifiers& spec, const syntax::FunctionTraits& traits,
                                 ir::Method& m) const {
        m.is_static = spec.is_static;
        m.is_virtual = spec.is_virtual;
        m.is_explicit = spec.is_explicit;
        m.is_const = traits.is_const;
        m.is_noexcept = traits.is_noexcept;
        m.is_override = traits.is_override;
        m.is_final = traits.is_final;
        m.ref_qualifier = traits.ref_qualifier;
        const Node value = node.child_by_field("default_value");
        // `= 0` is the default value of a declaration, but a clause of its own when the grammar reads the declaration
        // as a definition, which it does for a destructor: `virtual ~Listener() = 0;`.
        m.is_pure = (!value.is_null() && value.text() == "0") || has_child_of_type(node, "pure_virtual_clause");
        m.is_deleted = has_child_of_type(node, "delete_method_clause");
        m.is_defaulted = has_child_of_type(node, "default_method_clause");
    }

    void method(const Node& node, const Specifiers& spec, const Declarator& d, ClassState& state,
                bool is_template, bool invokable) {
        const syntax::FunctionTraits traits = syntax::read_function_traits(d.function, file_);

        ir::Method m;
        m.name = ir::normalize_operator_name(d.name);
        if (!m.name.empty() && m.name[0] == '~') {
            m.kind = ir::MethodKind::Destructor;
        } else if (m.name == state.cls.name && spec.type.is_null()) {
            m.kind = ir::MethodKind::Constructor;
        } else if (ir::is_operator_name(m.name)) {
            m.kind = ir::MethodKind::Operator;
        }
        if (m.kind == ir::MethodKind::Normal || m.kind == ir::MethodKind::Operator) {
            m.ret = traits.has_trailing_return ? traits.trailing_return
                                               : syntax::make_declared_type(spec, d, file_);
        }
        m.params = syntax::read_parameters(d.function.child_by_field("parameters"), file_);
        if (has_pointer_to_member_parameter(m.params)) {
            unsupported("'" + m.name + "' has a pointer-to-member parameter, which this reader cannot read; it is skipped", d.name_node);
            return;
        }
        m.access = state.access;
        m.qt_role = role_for(state, invokable);
        m.is_template = is_template;
        apply_declaration_flags(node, spec, traits, m);
        m.origin = ir::Origin::Declared;
        m.where = syntax::location(d.name_node, file_);
        if (has_child_of_type(d.function, "requires_clause")) {
            unsupported("the requires-clause of '" + m.name + "' is ignored", d.function);
        }
        add_method(state, std::move(m));
    }

    void conversion(const Node& node, const Node& cast, const Specifiers& outer, ClassState& state,
                    bool is_template, bool invokable) {
        const Specifiers target_spec = syntax::read_specifiers(cast);
        const Declarator target_declarator = syntax::parse_declarator(cast.child_by_field("declarator"));
        if (target_declarator.function.is_null()) {
            return;
        }
        const ir::Type target = syntax::make_declared_type(target_spec, target_declarator, file_);
        const syntax::FunctionTraits traits = syntax::read_function_traits(target_declarator.function, file_);

        ir::Method m;
        m.kind = ir::MethodKind::Conversion;
        m.name = "operator " + target.spelling;
        m.params = syntax::read_parameters(target_declarator.function.child_by_field("parameters"), file_);
        m.access = state.access;
        m.qt_role = role_for(state, invokable);
        m.is_template = is_template;
        apply_declaration_flags(node, outer, traits, m);
        m.origin = ir::Origin::Declared;
        m.where = syntax::location(cast, file_);
        add_method(state, std::move(m));
    }

    void field(const Specifiers& spec, const Declarator& d, ClassState& state) {
        for (const ir::Field& existing : state.cls.fields) {
            if (existing.name == d.name) return;
        }
        ir::Field f;
        f.type = syntax::make_declared_type(spec, d, file_);
        f.name = d.name;
        f.access = state.access;
        f.is_static = spec.is_static;
        f.origin = ir::Origin::Declared;
        f.where = syntax::location(d.name_node, file_);
        state.cls.fields.push_back(std::move(f));
    }

    const syntax::Tree&              tree_;
    const std::vector<QtMarker>&     markers_;
    const std::string&               file_;
    const std::string&               header_;
    ir::Model&                       model_;
    core::Diagnostics&               diagnostics_;
    bool                             anonymous_namespaces_;
    std::vector<std::string>         ns_;
    std::vector<Branch>                          branches_;   // the #if branches the walk is inside of
    std::size_t                                  guard_base_ = 0;   // how many of their conditions belong to the class around
    std::map<std::string, std::vector<Declared>> declared_;   // per class, what has been declared so far
};

}  // namespace

void read_declarations(const syntax::Tree& tree, const std::vector<syntax::QtMarker>& markers,
                       const std::string& file, const std::string& include_path, ir::Model& model,
                       core::Diagnostics& diagnostics, bool anonymous_namespaces) {
    Reader(tree, markers, file, include_path, model, diagnostics, anonymous_namespaces).run();
}

ir::Model read_header(const core::SourceFile& source, const std::string& include_path,
                      const syntax::MacroTable& macros, core::Diagnostics& diagnostics) {
    const syntax::Preprocessed pre = syntax::preprocess(source.text, macros);
    const syntax::Tree tree = syntax::parse_cpp(pre.text);
    ir::Model model;
    read_declarations(tree, pre.markers, source.path, include_path, model, diagnostics);
    return model;
}

}  // namespace mt::frontend
