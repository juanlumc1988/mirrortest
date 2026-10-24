// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/frontend/test_frontend.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <sstream>

#include "mirrortest/frontend/code_frontend.h"
#include "mirrortest/ir/type.h"
#include "mirrortest/syntax/extract.h"

namespace mt::frontend {

namespace {

using syntax::Node;
using Use = UseKind;

// ---- names and types ----------------------------------------------------------------------

ir::Type inferred_type(std::string_view spelling, const std::string& reason) {
    ir::Type t = ir::make_type(spelling);
    t.origin = ir::Origin::Inferred;
    t.reason = reason;
    return t;
}

std::optional<Receiver> receiver_of(const ValueInfo& v, bool arrow) {
    return v.type ? receiver_of_type(*v.type, arrow) : std::nullopt;
}

// *p, for a raw or smart pointer p.
std::optional<ir::Type> dereference(const ir::Type& t) {
    if (t.pointer_depth > 0) {
        ir::Type out = ir::make_type(t.base, t.is_const, ir::RefKind::None, t.pointer_depth - 1);
        out.origin = t.origin;
        out.reason = t.reason;
        out.where = t.where;
        return out;
    }
    const std::optional<TypeName> name = class_name_of(t);
    if (name && is_smart_pointer(*name)) {
        const std::string inner = first_template_argument(name->template_args);
        if (inner.empty()) return std::nullopt;
        const bool is_const = inner.compare(0, 6, "const ") == 0;
        ir::Type out = ir::make_type(is_const ? inner.substr(6) : inner, is_const);
        out.origin = t.origin;
        out.reason = t.reason;
        out.where = t.where;
        return out;
    }
    return std::nullopt;
}

// What `auto`, `auto&`, `const auto&` or `auto*` deduces from a value of type
// `value`: by value drops the top-level const and the reference, a reference or
// a pointer keeps the constness of what it binds to.
ir::Type deduce_auto(const ir::Type& declared, const ir::Type& value) {
    const bool keeps_const = value.pointer_depth > 0 || declared.ref != ir::RefKind::None;
    const bool is_const = declared.is_const || (keeps_const && value.is_const);
    ir::Type out = ir::make_type(value.base, is_const, declared.ref, value.pointer_depth);
    out.origin = ir::Origin::Declared;
    out.reason = value.reason;
    out.where = value.where;
    return out;
}

// ---- symbols ---------------------------------------------------------------------------------

struct Symbol {
    std::optional<ir::Type> type;   // none for `auto` that could not be deduced
    FactRef                 origin; // `auto n = foo.count();`: the call whose result n holds
};

class SymbolTable {
public:
    void clear() { scopes_.clear(); }
    void push() { scopes_.emplace_back(); }
    void pop() { if (!scopes_.empty()) scopes_.pop_back(); }
    void declare(const std::string& name, std::optional<ir::Type> type, FactRef origin = FactRef()) {
        if (scopes_.empty()) push();
        scopes_.back()[name] = Symbol{std::move(type), origin};
    }
    const Symbol* lookup(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end()) return &found->second;
        }
        return nullptr;
    }

private:
    std::vector<std::map<std::string, Symbol>> scopes_;
};

// ---- assertions --------------------------------------------------------------------------------------

enum class Assert {
    None,        // not an assertion macro
    Compare,     // EQ NE LT LE GT GE        (a, b)
    Truth,       // TRUE FALSE               (cond)
    CString,     // STREQ STRNE STRCASE*     (a, b)
    Float,       // FLOAT_EQ
    Double,      // DOUBLE_EQ
    Near,        // NEAR                     (a, b, abs_error)
    Throws,      // THROW                    (stmt, ExceptionType)
    Statement,   // ANY_THROW NO_THROW DEATH NO_FATAL_FAILURE ...   (stmt, ...)
    Matcher,     // THAT                     (value, matcher): the matcher is not analysed
    Skip,        // EXPECT_CALL, ON_CALL: mock set-up, not code under test
    Message,     // FAIL SUCCEED GTEST_SKIP ADD_FAILURE: carry only a streamed message
    Opaque,      // PRED*, ...: evaluate the arguments, say nothing about them
};

Assert classify_assertion(std::string_view name) {
    if (name == "EXPECT_CALL" || name == "ON_CALL" || name == "SCOPED_TRACE") return Assert::Skip;
    if (name == "FAIL" || name == "SUCCEED" || name == "GTEST_SKIP" || name == "ADD_FAILURE" ||
        name == "GTEST_FAIL" || name == "GTEST_SUCCEED") {
        return Assert::Message;
    }
    std::string_view rest;
    if (name.rfind("EXPECT_", 0) == 0) rest = name.substr(7);
    else if (name.rfind("ASSERT_", 0) == 0) rest = name.substr(7);
    else return Assert::None;

    if (rest == "EQ" || rest == "NE" || rest == "LT" || rest == "LE" || rest == "GT" || rest == "GE") return Assert::Compare;
    if (rest == "TRUE" || rest == "FALSE") return Assert::Truth;
    if (rest == "STREQ" || rest == "STRNE" || rest == "STRCASEEQ" || rest == "STRCASENE") return Assert::CString;
    if (rest == "FLOAT_EQ") return Assert::Float;
    if (rest == "DOUBLE_EQ") return Assert::Double;
    if (rest == "NEAR") return Assert::Near;
    if (rest == "THROW") return Assert::Throws;
    if (rest == "ANY_THROW" || rest == "NO_THROW" || rest == "DEATH" || rest == "EXIT" ||
        rest == "DEBUG_DEATH" || rest == "NO_FATAL_FAILURE" || rest == "NONFATAL_FAILURE" ||
        rest == "FATAL_FAILURE") {
        return Assert::Statement;
    }
    if (rest == "THAT") return Assert::Matcher;
    return Assert::Opaque;
}

bool is_literal_node(std::string_view type) {
    return type == "number_literal" || type == "char_literal" || type == "string_literal" ||
           type == "raw_string_literal" || type == "concatenated_string" ||
           type == "user_defined_literal" || type == "true" || type == "false" || type == "null";
}

bool is_named_cast(std::string_view name) {
    return name == "static_cast" || name == "reinterpret_cast" || name == "const_cast" ||
           name == "dynamic_cast";
}

// ---- the collector --------------------------------------------------------------------------------------

class Collector {
public:
    Collector(const syntax::Tree& tree, const std::string& file, const TestFrontendOptions& options,
              TestFileFacts& facts, core::Diagnostics& diagnostics)
        : tree_(tree), file_(file), options_(options), facts_(facts), diagnostics_(diagnostics) {}

    void run() {
        syntax::walk(tree_.root(), [&](const Node& n) {
            if (n.type() != "using_declaration") return true;
            std::string text(n.text().substr(5));   // after "using"
            text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return c == ';' || c == ' ' || c == '\n' || c == '\t'; }),
                       text.end());
            if (text.rfind("namespace", 0) == 0) {
                facts_.using_namespaces.push_back(text.substr(9));
                if (facts_.using_namespaces.back() == "std") using_std_ = true;
            } else if (!text.empty()) {
                facts_.using_declarations.push_back(parse_type_name(text));
            }
            return true;
        });
        top_level(tree_.root());
    }

private:
    // ---- the shape of the file ---------------------------------------------------------------

    ir::SourceLoc loc(const Node& n) const { return syntax::location(n, file_); }

    const ir::Class* local_class(const std::string& name) const {
        const std::string last = parse_type_name(name).name;
        for (const ir::Class& c : facts_.local.classes) {
            if (c.name == last) return &c;
        }
        return nullptr;
    }

    static bool is_conditional(std::string_view t) {
        return t == "preproc_if" || t == "preproc_ifdef" || t == "preproc_else" ||
               t == "preproc_elif" || t == "preproc_elifdef";
    }

    void top_level(const Node& scope) {
        for (const Node& child : scope.named_children()) {
            const std::string_view type = child.type();
            if (type == "namespace_definition") {
                const Node name = child.child_by_field("name");
                std::vector<std::string> parts;
                std::string_view rest = name.text();
                for (std::size_t sep; !name.is_null() && (sep = rest.find("::")) != std::string_view::npos;) {
                    parts.emplace_back(rest.substr(0, sep));
                    rest.remove_prefix(sep + 2);
                }
                if (!name.is_null()) parts.emplace_back(rest);
                for (const std::string& p : parts) ns_.push_back(p);
                top_level(child.child_by_field("body"));
                ns_.resize(ns_.size() - parts.size());
            } else if (is_conditional(type)) {
                top_level(child);
            } else if (type == "template_declaration") {
                // The template parameters are names the file declares, not types of
                // the code under test, for as long as the declaration lasts.
                const std::size_t mark = template_params_.size();
                collect_template_params(child.child_by_field("parameters"));
                top_level(child);
                template_params_.resize(mark);
            } else if (type == "alias_declaration" || type == "type_definition") {
                alias(child);
            } else if (type == "linkage_specification") {
                const Node body = child.child_by_field("body");
                if (body.type() == "declaration_list") top_level(body);
            } else if (type == "function_definition") {
                function_definition(child);
            } else if (type == "class_specifier" || type == "struct_specifier") {
                class_bodies(child);
            } else if (type == "declaration") {
                const Node specifier = child.child_by_field("type");
                if (specifier.type() == "class_specifier" || specifier.type() == "struct_specifier") {
                    class_bodies(specifier);
                }
            }
        }
    }

    void collect_template_params(const Node& list) {
        for (const Node& p : list.named_children()) {
            const std::string_view type = p.type();
            if (type.find("type_parameter_declaration") == std::string_view::npos) continue;
            for (const Node& c : p.named_children()) {
                if (c.type() == "type_identifier") {
                    template_params_.emplace_back(c.text());
                    break;
                }
            }
        }
    }

    // `using Vec = std::vector<Foo>;`, `typedef unsigned long Word;`: the name is
    // the file's own, the type it stands for is a use.
    void alias(const Node& n) {
        if (n.type() == "alias_declaration") {
            const Node name = n.child_by_field("name");
            if (!name.is_null()) facts_.local_type_names.emplace_back(name.text());
        } else {
            const syntax::Declarator d = syntax::parse_declarator(n.child_by_field("declarator"));
            const Node declarator = n.child_by_field("declarator");
            if (!d.name.empty()) facts_.local_type_names.push_back(d.name);
            else if (declarator.type() == "type_identifier") facts_.local_type_names.emplace_back(declarator.text());
        }
        add_type_uses(n.child_by_field("type"));
    }

    // The inline methods of a class the test file defines (a fixture).
    void class_bodies(const Node& spec) {
        const Node name = spec.child_by_field("name");
        const Node body = spec.child_by_field("body");
        if (name.is_null() || body.is_null()) return;
        const ir::Class* cls = local_class(std::string(name.text()));
        for (const Node& member : body.named_children()) {
            if (member.type() == "function_definition") {
                const syntax::Declarator d = syntax::parse_declarator(member.child_by_field("declarator"));
                if (!d.function.is_null() && !member.child_by_field("body").is_null()) {
                    add_type_uses(member.child_by_field("type"));
                    analyse(member.child_by_field("body"), d.function.child_by_field("parameters"), cls, std::nullopt,
                            initialiser_list(member));
                }
            } else if (member.type() == "field_declaration") {
                field_declaration(member);
            }
            // Members under #if are not visited: the code frontend read them for the
            // symbol table, and their bodies are too rare to be worth the nesting.
        }
    }

    static Node initialiser_list(const Node& def) {
        for (const Node& child : def.named_children()) {
            if (child.type() == "field_initializer_list") return child;
        }
        return Node();
    }

    // A by-value member of class type is constructed whenever its owner is: a
    // fixture's `Foo foo_;` needs Foo to be default constructible, `Baz c_{1};`
    // needs Baz(int). Static members are defined elsewhere and say nothing here.
    void field_declaration(const Node& n) {
        const syntax::Specifiers spec = syntax::read_specifiers(n);
        add_type_uses(spec.type);
        if (spec.is_static) return;

        for (const Node& declarator : syntax::children_by_field(n, "declarator")) {
            const syntax::Declarator d = syntax::parse_declarator(declarator);
            if (d.name_node.is_null() || !d.function.is_null()) continue;
            const std::optional<TypeName> object = by_value_class(syntax::make_declared_type(spec, d, file_));
            if (!object) continue;

            const Node init = n.child_by_field("default_value");
            if (init.is_null()) {
                add_construction(*object, Construction::Form::Default, {}, n);
            } else if (init.type() == "initializer_list") {
                add_construction(*object, Construction::Form::Brace, arguments(init), init);
            } else {
                (void)initializer(init, object);
            }
        }
    }

    // `Fixture() : foo_(3), bar_{4} {}`
    void member_initialisers(const Node& list) {
        if (list.is_null() || owner_ == nullptr) return;
        for (const Node& init : list.named_children()) {
            if (init.type() != "field_initializer" || init.named_child_count() < 2) continue;
            const std::string name(init.named_child(0).text());
            const Node args = init.named_child(1);
            const ir::Field* field = nullptr;
            for (const ir::Field& f : owner_->fields) {
                if (f.name == name) field = &f;
            }
            if (field != nullptr) {
                (void)initialise_with(field->type, arguments(args),
                                      args.type() == "argument_list" ? Construction::Form::Direct : Construction::Form::Brace,
                                      args);
            } else {
                (void)arguments(args);   // a base class: its arguments are still code to read
            }
        }
    }

    void function_definition(const Node& def) {
        const syntax::Declarator d = syntax::parse_declarator(def.child_by_field("declarator"));
        const Node body = def.child_by_field("body");
        if (d.function.is_null() || body.is_null() || d.is_function_pointer) return;
        const Node params = d.function.child_by_field("parameters");

        if (d.name != "TEST" && d.name != "TEST_F" && d.name != "TEST_P") add_type_uses(def.child_by_field("type"));
        if (d.name == "TEST" || d.name == "TEST_F" || d.name == "TEST_P") {
            test_macro(def, d, params, body);
        } else if (d.name == "TYPED_TEST" || d.name == "TYPED_TEST_P") {
            diagnostics_.warning(core::code::unsupported,
                                 d.name + " is not supported; its body is skipped", loc(d.name_node));
        } else if (const std::size_t sep = d.name.rfind("::"); sep != std::string::npos) {
            // void Fixture::SetUp() { ... }: the body sees Fixture's members.
            analyse(body, params, local_class(d.name.substr(0, sep)), std::nullopt, initialiser_list(def));
        } else {
            analyse(body, params, nullptr, std::nullopt);
        }
    }

    void test_macro(const Node& def, const syntax::Declarator& d, const Node& params, const Node& body) {
        const std::vector<Node> names = params.named_children();
        if (names.size() != 2) return;

        TestCaseInfo info;
        info.kind = d.name;
        info.suite = std::string(names[0].child_by_field("type").text());
        info.name = std::string(names[1].child_by_field("type").text());
        info.where = loc(def);
        info.ns = ns_;
        facts_.tests.push_back(info);

        const ir::Class* fixture = nullptr;
        if (d.name != "TEST") {
            fixture = local_class(info.suite);
            if (fixture == nullptr) {
                diagnostics_.warning(core::code::unknown_fixture,
                                     "the fixture '" + info.suite + "' is not defined in this file; "
                                     "its members are unknown",
                                     loc(names[0]));
            }
        }
        analyse(body, Node(), fixture, std::nullopt);
    }

    // ---- one body -------------------------------------------------------------------------------

    void declare_members(const ir::Class& cls, std::set<std::string>& visited) {
        if (!visited.insert(cls.name).second) return;
        for (const ir::BaseSpec& base : cls.bases) {
            if (const ir::Class* parent = local_class(base.name)) {
                declare_members(*parent, visited);
            }
            // testing::TestWithParam<T> and WithParamInterface<T> give GetParam() its type.
            const TypeName parsed = parse_type_name(base.name);
            if ((parsed.name == "TestWithParam" || parsed.name == "WithParamInterface") &&
                !parsed.template_args.empty()) {
                ir::Type t = ir::make_type(first_template_argument(parsed.template_args));
                t.origin = ir::Origin::Declared;
                t.reason = "GetParam() of " + cls.name;
                param_type_ = t;
            }
        }
        for (const ir::Field& f : cls.fields) {
            symbols_.declare(f.name, f.type);
        }
    }

    void analyse(const Node& body, const Node& params, const ir::Class* owner,
                 std::optional<ir::Type> param_type, const Node& initialisers = Node()) {
        symbols_.clear();
        owner_ = owner;
        param_type_ = std::move(param_type);

        symbols_.push();
        if (owner != nullptr) {
            std::set<std::string> visited;
            declare_members(*owner, visited);
        }
        symbols_.push();
        declare_parameters(params);
        member_initialisers(initialisers);
        statement(body);
        symbols_.clear();
        owner_ = nullptr;
        param_type_.reset();
    }

    // The parameters of a function, lambda or catch clause: each type is a use,
    // each name a symbol.
    void declare_parameters(const Node& list) {
        if (list.is_null()) return;
        for (const Node& p : list.named_children()) {
            if (p.type() == "parameter_declaration" || p.type() == "optional_parameter_declaration") {
                add_type_uses(p.child_by_field("type"));
            }
        }
        for (const ir::Param& p : syntax::read_parameters(list, file_)) {
            if (!p.name.empty()) declare_typed(p.name, p.type);
        }
    }

    void declare_typed(const std::string& name, const ir::Type& type) {
        if (type.base == "auto") {
            symbols_.declare(name, std::nullopt);
        } else {
            symbols_.declare(name, type);
        }
    }

    // ---- values and their uses -------------------------------------------------------------------

    ValueInfo value_of(const Node& n) const {
        ValueInfo v;
        v.text = std::string(n.text());
        v.where = loc(n);
        return v;
    }

    ResultUse* slot_of(const FactRef& ref) {
        switch (ref.kind) {
            case FactRef::Kind::MethodCall:  return &facts_.method_calls[ref.index].use;
            case FactRef::Kind::ScopedCall:  return &facts_.scoped_calls[ref.index].use;
            case FactRef::Kind::FreeCall:    return &facts_.free_calls[ref.index].use;
            case FactRef::Kind::FieldAccess: return &facts_.field_accesses[ref.index].use;
            case FactRef::Kind::Unresolved:  return &facts_.unresolved[ref.index].call.use;
            case FactRef::Kind::None:        return nullptr;
        }
        return nullptr;
    }

    void apply_use(const ValueInfo& v, const ResultUse& use) {
        if (ResultUse* slot = slot_of(v.from)) {
            if (slot->kind == Use::Unknown) *slot = use;   // the first context to claim a result wins
        }
        refine_auto(v, use);
    }

    // `auto n = foo.count(); EXPECT_EQ(n, 3u);`: the comparison is evidence about
    // count(), as good as `EXPECT_EQ(foo.count(), 3u)`. The first hint found wins;
    // contexts that carry none (an argument, a discarded value) add nothing.
    void refine_auto(const ValueInfo& v, const ResultUse& use) {
        ResultUse* slot = slot_of(v.auto_origin);
        if (slot == nullptr || slot->kind != Use::AutoBound || slot->hint || !use.hint) return;
        if (use.kind != Use::Compared && use.kind != Use::Condition && use.kind != Use::Assigned) return;
        ir::Type hint = *use.hint;
        hint.reason += (hint.reason.empty() ? "" : ", ") + std::string("through variable '") + v.var_name + "'";
        slot->hint = std::move(hint);
    }

    static ResultUse use_of(Use kind, std::optional<ir::Type> hint = std::nullopt) {
        ResultUse u;
        u.kind = kind;
        u.hint = std::move(hint);
        return u;
    }

    ir::Type bool_hint(const std::string& reason) const { return inferred_type("bool", reason); }

    // What the other side of a comparison says the result is.
    std::optional<ir::Type> hint_from(const ValueInfo& other, const std::string& assertion) const {
        if (other.is_string_literal) {
            return inferred_type("std::string", "string literal in " + assertion);
        }
        if (other.type) {
            ir::Type t = *other.type;
            if (t.reason.empty()) t.reason = "compared in " + assertion;
            return t;
        }
        return std::nullopt;
    }

    void compare(const ValueInfo& a, const ValueInfo& b, const std::string& assertion) {
        ResultUse for_a = use_of(Use::Compared, hint_from(b, assertion));
        for_a.hint_warnings = b.warnings;
        ResultUse for_b = use_of(Use::Compared, hint_from(a, assertion));
        for_b.hint_warnings = a.warnings;
        apply_use(a, for_a);
        apply_use(b, for_b);
    }

    // ---- type uses ----------------------------------------------------------------------------------

    void add_type_use(const TypeName& name, const Node& where, bool from_throw = false) {
        if (name.empty() || is_fundamental_name(name.name)) return;
        if (name.scope.empty() && std::find(template_params_.begin(), template_params_.end(), name.name) != template_params_.end()) {
            return;
        }
        TypeUse use;
        use.type = name;
        use.type.template_args.clear();
        use.from_throw_assertion = from_throw;
        use.where = loc(where);
        use.ns = ns_;
        facts_.type_uses.push_back(std::move(use));
    }

    // Every type named in a declared type: `std::vector<Foo>` uses vector and Foo.
    void add_type_uses(const Node& n, bool from_throw = false) {
        if (n.is_null()) return;
        const std::string_view type = n.type();
        // In EXPECT_THROW(stmt, MyError) the type is an argument of a macro call, so
        // the parser calls a bare name an `identifier` rather than a type.
        if (type == "type_identifier" || (from_throw && type == "identifier")) {
            add_type_use(parse_type_name(n.text()), n, from_throw);
        } else if (type == "qualified_identifier" || type == "template_type") {
            std::string_view text = n.text();
            const std::size_t open = text.find('<');
            add_type_use(parse_type_name(open == std::string_view::npos ? text : text.substr(0, open)), n, from_throw);
            add_template_argument_uses(n);
        } else if (type == "type_descriptor") {
            add_type_uses(n.child_by_field("type"), from_throw);
        } else if (type == "struct_specifier" || type == "class_specifier" || type == "enum_specifier") {
            add_type_use(parse_type_name(n.child_by_field("name").text()), n, from_throw);
        }
    }

    void add_template_argument_uses(const Node& n) {
        Node args;
        if (n.type() == "template_type") {
            args = n.child_by_field("arguments");
        } else if (n.type() == "qualified_identifier") {
            const Node inner = n.child_by_field("name");
            if (inner.type() == "template_type") args = inner.child_by_field("arguments");
            else if (inner.type() == "qualified_identifier") add_template_argument_uses(inner);
        }
        for (const Node& a : args.named_children()) {
            if (a.type() == "type_descriptor") add_type_uses(a);
        }
    }

    // ---- statements ------------------------------------------------------------------------------------

    void statement(const Node& n) {
        if (n.is_null()) return;
        const std::string_view type = n.type();

        if (type == "compound_statement") {
            symbols_.push();
            for (const Node& child : n.named_children()) statement(child);
            symbols_.pop();
        } else if (type == "declaration") {
            declaration(n);
        } else if (type == "expression_statement") {
            expression_statement(n);
        } else if (type == "alias_declaration" || type == "type_definition") {
            alias(n);
        } else if (type == "for_range_loop") {
            range_for(n);
        } else if (type == "if_statement" || type == "while_statement" || type == "switch_statement") {
            symbols_.push();
            for (const Node& child : n.named_children()) {
                // A switch tests an integer, not a bool.
                if (child.type() == "condition_clause") condition(child, type != "switch_statement");
                else statement(child);
            }
            symbols_.pop();
        } else if (type == "for_statement") {
            for_loop(n);
        } else if (type == "do_statement") {
            statement(n.child_by_field("body"));
            const ValueInfo c = expr(n.child_by_field("condition"));
            apply_use(c, use_of(Use::Condition, bool_hint("condition")));
        } else if (type == "return_statement" || type == "throw_statement") {
            for (const Node& child : n.named_children()) value_statement(child);
        } else if (type == "try_statement" || type == "catch_clause" || type == "else_clause" ||
                   type == "labeled_statement" || type == "case_statement" || type == "seh_try_statement") {
            symbols_.push();
            for (const Node& child : n.named_children()) {
                if (child.type() == "parameter_list") {
                    declare_parameters(child);
                } else {
                    statement(child);
                }
            }
            symbols_.pop();
        } else if (type != "comment") {
            value_statement(n);
        }
    }

    // A child that is an expression where a statement was expected (a `for`
    // update, a `return` value): evaluated for the facts inside it.
    void value_statement(const Node& n) {
        const std::string_view type = n.type();
        if (type == "comment" || type == "compound_statement") {
            if (type == "compound_statement") statement(n);
            return;
        }
        (void)expr(n);
    }

    // `(cond)`, `(T v = e)` or the C++17 `(init; cond)`.
    void condition(const Node& clause, bool boolean) {
        std::optional<ValueInfo> last;
        for (const Node& child : clause.named_children()) {
            const std::string_view type = child.type();
            if (type == "init_statement") {
                for (const Node& s : child.named_children()) statement(s);
            } else if (type == "declaration") {
                declaration(child);
            } else if (type != "comment") {
                last = expr(child);
            }
        }
        if (boolean && last) apply_use(*last, use_of(Use::Condition, bool_hint("condition")));
    }

    void for_loop(const Node& n) {
        symbols_.push();
        statement(n.child_by_field("initializer"));
        const Node cond = n.child_by_field("condition");
        if (!cond.is_null()) {
            const ValueInfo c = expr(cond);
            apply_use(c, use_of(Use::Condition, bool_hint("condition")));
        }
        value_statement(n.child_by_field("update"));
        statement(n.child_by_field("body"));
        symbols_.pop();
    }

    void expression_statement(const Node& n) {
        if (n.named_child_count() == 0) return;
        Node e = n.named_child(0);

        // EXPECT_EQ(a, b) << "message" << more;
        std::vector<Node> streamed;
        Node core = e;
        while (core.type() == "binary_expression" && core.child_by_field("operator").text() == "<<" &&
               assertion_in(core.child_by_field("left")) != Assert::None) {
            streamed.push_back(core.child_by_field("right"));
            core = core.child_by_field("left");
        }
        if (core.type() == "call_expression" && assertion(core)) {
            for (const Node& s : streamed) (void)expr(s);
            return;
        }
        const ValueInfo v = expr(e);
        apply_use(v, use_of(Use::Discarded));
    }

    // EXPECT_CALL(m, f(1)).WillOnce(Return(3)): a chain rooted in a macro that
    // only sets up a mock. None of it -- `Return`, `Times`, `_` -- is code under test.
    bool is_mock_setup(const Node& n) const {
        Node cur = n;
        for (;;) {
            if (cur.type() == "call_expression") cur = cur.child_by_field("function");
            else if (cur.type() == "field_expression") cur = cur.child_by_field("argument");
            else break;
        }
        return cur.type() == "identifier" && classify_assertion(cur.text()) == Assert::Skip;
    }

    Assert assertion_in(const Node& n) const {
        Node core = n;
        while (core.type() == "binary_expression" && core.child_by_field("operator").text() == "<<") {
            core = core.child_by_field("left");
        }
        if (core.type() != "call_expression") return Assert::None;
        const Node fn = core.child_by_field("function");
        return fn.type() == "identifier" ? classify_assertion(fn.text()) : Assert::None;
    }

    // Returns true if the call was an assertion macro and has been handled.
    bool assertion(const Node& call) {
        const Node fn = call.child_by_field("function");
        if (fn.type() != "identifier") return false;
        const std::string name(fn.text());
        const Assert kind = classify_assertion(name);
        if (kind == Assert::None) return false;

        const std::vector<Node> args = call.child_by_field("arguments").named_children();
        const auto arg = [&](std::size_t i) -> Node { return i < args.size() ? args[i] : Node(); };

        switch (kind) {
            case Assert::Skip:
            case Assert::Message:
            case Assert::None:
                break;
            case Assert::Compare: {
                const ValueInfo a = expr(arg(0));
                const ValueInfo b = expr(arg(1));
                compare(a, b, name);
                break;
            }
            case Assert::Truth: {
                const ValueInfo a = expr(arg(0));
                apply_use(a, use_of(Use::Condition, bool_hint(name)));
                break;
            }
            case Assert::CString: {
                const ValueInfo a = expr(arg(0));
                const ValueInfo b = expr(arg(1));
                const auto narrow_or_wide = [&](const ValueInfo& other) {
                    const bool wide = other.type && other.type->base == "wchar_t";
                    ir::Type t = ir::make_type(wide ? "wchar_t" : "char", true, ir::RefKind::None, 1);
                    t.origin = ir::Origin::Inferred;
                    t.reason = name;
                    return t;
                };
                apply_use(a, use_of(Use::Compared, narrow_or_wide(b)));
                apply_use(b, use_of(Use::Compared, narrow_or_wide(a)));
                break;
            }
            case Assert::Float:
            case Assert::Double:
            case Assert::Near: {
                const char* type = kind == Assert::Float ? "float" : "double";
                const ValueInfo a = expr(arg(0));
                const ValueInfo b = expr(arg(1));
                apply_use(a, use_of(Use::Compared, inferred_type(type, name)));
                apply_use(b, use_of(Use::Compared, inferred_type(type, name)));
                if (kind == Assert::Near) (void)expr(arg(2));
                break;
            }
            case Assert::Throws:
                statement_argument(arg(0));
                add_type_uses(arg(1), true);
                break;
            case Assert::Statement:
                statement_argument(arg(0));
                break;
            case Assert::Matcher: {
                const ValueInfo a = expr(arg(0));
                (void)a;   // the matcher says what it says in gmock's own language
                break;
            }
            case Assert::Opaque:
                for (const Node& a : args) (void)expr(a);
                break;
        }
        return true;
    }

    // EXPECT_THROW(stmt, E): `stmt` is a statement, possibly a {...} block.
    void statement_argument(const Node& n) {
        if (n.is_null()) return;
        if (n.type() == "compound_statement") {
            statement(n);
            return;
        }
        const ValueInfo v = expr(n);
        apply_use(v, use_of(Use::Discarded));
    }

    // ---- declarations ---------------------------------------------------------------------------------------

    void declaration(const Node& n) {
        const syntax::Specifiers spec = syntax::read_specifiers(n);
        add_type_uses(spec.type);

        for (const Node& declarator : syntax::children_by_field(n, "declarator")) {
            const syntax::Declarator d = syntax::parse_declarator(declarator);
            if (d.name_node.is_null()) continue;

            // tree-sitter reads `Foo f(x);` and `Foo f(move(p));` as function
            // declarations -- `x` could be a type with an unnamed parameter -- and
            // `Foo f();` really is one. Inside a function body the first two are
            // the initialisations they were meant to be; the third is the most
            // vexing parse.
            std::optional<std::vector<ValueInfo>> reread;
            Node reread_list;
            if (!d.function.is_null() && !d.is_function_pointer) {
                const Node params = d.function.child_by_field("parameters");
                if (params.named_child_count() == 0) {
                    if (!spec.type.is_null() && spec.type.type() != "primitive_type") {
                        diagnostics_.warning(core::code::vexing_parse,
                                             "'" + std::string(n.text()) + "' declares a function, not an object "
                                             "(the most vexing parse); the constructor call is not seen",
                                             loc(n));
                    }
                    continue;
                }
                if (!is_expression_list(params)) continue;   // a genuine function declaration
                reread = values_of_parameters(params);
                reread_list = params;
            }

            const ir::Type type = syntax::make_declared_type(spec, d, file_);
            std::optional<ir::Type> symbol_type = type.base == "auto" ? std::nullopt : std::optional<ir::Type>(type);

            // `T v = e;` keeps the value in the init_declarator; the declaration
            // in `if (T v = e)` carries it itself.
            const Node init = declarator.type() == "init_declarator" ? declarator.child_by_field("value")
                                                                     : n.child_by_field("value");
            const std::optional<TypeName> object = by_value_class(type);

            Deduced deduced;
            if (reread) {
                deduced = initialise_with(type, std::move(*reread), Construction::Form::Direct, reread_list);
            } else if (init.is_null()) {
                if (object) add_construction(*object, Construction::Form::Default, {}, n);
            } else if (init.type() == "argument_list" || init.type() == "initializer_list") {
                deduced = initialise_with(type, arguments(init),
                                          init.type() == "argument_list" ? Construction::Form::Direct : Construction::Form::Brace,
                                          init);
            } else {
                deduced = bind(type, initializer(init, object));
            }
            if (deduced.type) symbol_type = deduced.type;
            symbols_.declare(d.name, std::move(symbol_type), deduced.origin);
        }
    }

    static std::optional<TypeName> by_value_class(const ir::Type& type) {
        if (type.pointer_depth != 0 || type.ref != ir::RefKind::None) return std::nullopt;
        return class_name_of(type);
    }

    // What `auto` turned out to be: a type when the value says one (a literal, a
    // make_unique), the call it holds when it is a call result.
    struct Deduced {
        std::optional<ir::Type> type;
        FactRef                 origin;
    };

    // A value meets the type it initialises: `T v = e;` and, for a T that is not
    // a class, `T v(e);` and `T v{e};`.
    Deduced bind(const ir::Type& type, const ValueInfo& v) {
        if (type.base != "auto") {
            apply_use(v, use_of(Use::Assigned, type));
            return {};
        }
        apply_use(v, use_of(Use::AutoBound));
        Deduced out;
        if (v.type && !v.from.valid()) out.type = deduce_auto(type, *v.type);   // auto n = 3u: the literal says
        else out.origin = v.from;
        return out;
    }

    // `T v(args)`, `T v{args}`, a member initialiser `v_(args)`: a class is
    // constructed with the arguments; anything else is initialised by the one value.
    Deduced initialise_with(const ir::Type& type, std::vector<ValueInfo> args, Construction::Form form,
                            const Node& where) {
        if (const std::optional<TypeName> object = by_value_class(type)) {
            add_construction(*object, form, std::move(args), where);
            return {};
        }
        if (args.size() == 1) return bind(type, args[0]);
        return {};
    }

    // ---- `Foo f(x);` read as an initialisation ---------------------------------------------------------

    // True if every parameter could just as well be an expression: a bare name
    // (`x`, `Color::Red`) or a call of one (`move(p)`, `make()`, `ns::f(a, b)`).
    // Anything only a declaration can contain -- const, a primitive type, `*`/`&`,
    // a parameter with both a type and a name, a default -- keeps it a declaration.
    static bool is_expression_list(const Node& list) {
        if (list.named_child_count() == 0) return false;
        for (const Node& p : list.named_children()) {
            if (p.type() == "comment") continue;
            if (!is_expression_parameter(p)) return false;
        }
        return true;
    }

    static bool is_expression_parameter(const Node& p) {
        if (p.type() != "parameter_declaration") return false;
        for (const Node& c : p.named_children()) {
            if (c.type() == "type_qualifier") return false;
        }
        const Node type = p.child_by_field("type");
        const std::string_view type_kind = type.type();
        if (type_kind != "type_identifier" && type_kind != "qualified_identifier") return false;
        if (type.text().find('<') != std::string_view::npos) return false;
        const Node declarator = p.child_by_field("declarator");
        if (declarator.is_null()) return true;
        if (declarator.type() != "abstract_function_declarator" && declarator.type() != "function_declarator") return false;
        const Node inner = declarator.child_by_field("parameters");
        return inner.named_child_count() == 0 || is_expression_list(inner);
    }

    std::vector<ValueInfo> values_of_parameters(const Node& list) {
        std::vector<ValueInfo> out;
        for (const Node& p : list.named_children()) {
            if (p.type() == "parameter_declaration") out.push_back(value_of_parameter(p));
        }
        return out;
    }

    // `move(p)` that tree-sitter took for a parameter of type `move` named `(p)`.
    ValueInfo value_of_parameter(const Node& p) {
        const Node type = p.child_by_field("type");
        const Node declarator = p.child_by_field("declarator");
        if (declarator.is_null()) {
            return type.type() == "type_identifier" ? identifier_value(type) : value_of(type);
        }
        std::vector<ValueInfo> args = values_of_parameters(declarator.child_by_field("parameters"));
        return type.type() == "type_identifier" ? free_call_with(p, type, std::move(args))
                                                : scoped_call_with(p, type, std::move(args));
    }

    // `T v = init;`
    ValueInfo initializer(const Node& init, const std::optional<TypeName>& object) {
        // `Foo g = Foo(3);` constructs (recorded by free_call); `Foo f = 5;`
        // constructs through a converting constructor; `Foo f = other;` copies --
        // unless `other` is not a Foo, which is a converting constructor again.
        const std::string saved = expected_constructor_;
        if (object) expected_constructor_ = object->name;
        ValueInfo v = expr(init);
        expected_constructor_ = saved;

        if (object && !v.from.valid() && init.type() != "call_expression") {
            const std::optional<TypeName> other = v.type ? class_name_of(*v.type) : std::nullopt;
            const bool another_type = v.type && !(other && other->name == object->name);
            if (is_literal_node(init.type()) || another_type) {
                add_construction(*object, Construction::Form::Copy, {v}, init);
            }
        }
        return v;
    }

    void range_for(const Node& n) {
        symbols_.push();
        const syntax::Specifiers spec = syntax::read_specifiers(n);
        add_type_uses(spec.type);
        const Node declarator = n.child_by_field("declarator");
        const syntax::Declarator d = syntax::parse_declarator(declarator);
        const Node range = n.child_by_field("right");
        if (!range.is_null()) (void)expr(range);
        if (!d.name_node.is_null()) {
            declare_typed(d.name, syntax::make_declared_type(spec, d, file_));
        }
        statement(n.child_by_field("body"));
        symbols_.pop();
    }

    void add_construction(const TypeName& type, Construction::Form form, std::vector<ValueInfo> args, const Node& where) {
        Construction c;
        c.type = type;
        c.form = form;
        c.args = std::move(args);
        c.where = loc(where);
        c.ns = ns_;
        for (const ValueInfo& a : c.args) apply_use(a, use_of(Use::Argument));
        facts_.constructions.push_back(std::move(c));
    }

    // ---- expressions ---------------------------------------------------------------------------------------------

    std::vector<ValueInfo> arguments(const Node& list) {
        std::vector<ValueInfo> out;
        for (const Node& a : list.named_children()) {
            if (a.type() != "comment") out.push_back(expr(a));
        }
        return out;
    }

    std::vector<ValueInfo> elements(const Node& list) { return arguments(list); }

    ValueInfo literal_value(const Node& n) {
        ValueInfo v = value_of(n);
        const std::optional<Literal> lit = classify_literal(n, options_.standard, file_);
        if (!lit) return v;
        v.warnings = lit->warnings;
        v.is_string_literal = lit->kind == Literal::Kind::String && lit->type.empty();
        v.is_null_pointer = lit->kind == Literal::Kind::NullPointer;
        if (!lit->type.empty()) v.type = lit->type;
        return v;
    }

    ValueInfo expr(const Node& n) {
        if (n.is_null()) return ValueInfo();
        const std::string_view type = n.type();

        if (is_literal_node(type)) return literal_value(n);
        if ((type == "call_expression" || type == "field_expression") && is_mock_setup(n)) {
            return value_of(n);   // EXPECT_CALL(m, f(1)).Times(2): set-up, not code under test
        }
        if (type == "identifier") return identifier_value(n);
        if (type == "this") {
            ValueInfo v = value_of(n);
            if (owner_ != nullptr) {
                ir::Type t = ir::make_type(owner_->name, false, ir::RefKind::None, 1);
                t.origin = ir::Origin::Declared;
                v.type = t;
            }
            return v;
        }
        if (type == "parenthesized_expression") {
            return n.named_child_count() > 0 ? expr(n.named_child(0)) : value_of(n);
        }
        if (type == "call_expression") return call(n);
        if (type == "field_expression") return field_value(n);
        if (type == "new_expression") return new_expression(n);
        if (type == "sizeof_expression") {
            for (const Node& c : n.named_children()) (void)expr(c);
            ValueInfo v = value_of(n);
            v.type = inferred_type("std::size_t", "sizeof");
            return v;
        }
        if (type == "cast_expression") return cast(n);
        if (type == "compound_literal_expression") return compound_literal(n);
        if (type == "unary_expression") return unary(n);
        if (type == "pointer_expression") return pointer_expression(n);
        if (type == "binary_expression") return binary(n);
        if (type == "conditional_expression") return conditional(n);
        if (type == "assignment_expression") return assignment(n);
        if (type == "lambda_expression") return lambda(n);
        if (type == "comment") return ValueInfo();

        // Anything else (subscripts, casts to templates, initializer lists, ...):
        // look inside for calls on the code under test, say nothing of the value.
        ValueInfo v = value_of(n);
        for (const Node& c : n.named_children()) (void)expr(c);
        return v;
    }

    ValueInfo identifier_value(const Node& n) {
        ValueInfo v = value_of(n);
        v.var_name = v.text;
        if (const Symbol* s = symbols_.lookup(v.text)) {
            v.is_lvalue = true;
            v.auto_origin = s->origin;
            if (s->type) {
                ir::Type t = *s->type;
                if (t.reason.empty()) t.reason = "variable '" + v.text + "'";
                v.type = t;
            }
        }
        return v;
    }

    ValueInfo unary(const Node& n) {
        const std::string op(n.child(0).text());
        const Node operand = n.child_by_field("argument");
        ValueInfo inner = expr(operand);

        if ((op == "-" || op == "+") && operand.type() == "number_literal") {
            // `- 3`: the same literal with a sign.
            const Literal lit = classify_number(op + std::string(operand.text()), options_.standard);
            ValueInfo v = value_of(n);
            v.warnings = lit.warnings;
            if (!lit.type.empty()) {
                v.type = lit.type;
                v.type->where = loc(n);
            }
            return v;
        }
        if (op == "!") {
            apply_use(inner, use_of(Use::Condition, bool_hint("operand of !")));
            ValueInfo v = value_of(n);
            v.type = inferred_type("bool", "result of !");
            return v;
        }
        inner.text = std::string(n.text());
        inner.from = FactRef();   // -foo.size() is not foo.size()
        return inner;
    }

    ValueInfo pointer_expression(const Node& n) {
        const std::string op(n.child(0).text());
        ValueInfo inner = expr(n.child_by_field("argument"));
        ValueInfo v = value_of(n);
        if (op == "*" && inner.type) {
            v.type = dereference(*inner.type);
            v.is_lvalue = true;
        } else if (op == "&") {
            if (inner.type) {
                ir::Type t = ir::make_type(inner.type->base, inner.type->is_const, ir::RefKind::None,
                                           inner.type->pointer_depth + 1);
                t.origin = inner.type->origin;
                t.reason = inner.type->reason;
                t.where = inner.type->where;
                v.type = t;
            }
            v.is_address_of = true;
            v.var_name = inner.var_name;
        }
        return v;
    }

    ValueInfo cast(const Node& n) {
        ValueInfo inner = expr(n.child_by_field("value"));
        (void)inner;
        const Node descriptor = n.child_by_field("type");
        ValueInfo v = value_of(n);
        if (!descriptor.is_null()) {
            add_type_uses(descriptor);
            ir::Type t = syntax::make_declared_type(syntax::read_specifiers(descriptor),
                                                    syntax::parse_declarator(descriptor.child_by_field("declarator")), file_);
            if (!t.empty()) v.type = t;
        }
        return v;
    }

    ValueInfo compound_literal(const Node& n) {
        // std::size_t{3}, Foo{1, 2}
        const Node type_node = n.child_by_field("type");
        const Node value = n.child_by_field("value");
        const std::vector<ValueInfo> args = elements(value);
        ValueInfo v = value_of(n);
        if (!type_node.is_null()) {
            add_type_uses(type_node);
            ir::Type t = ir::make_type(ir::normalize_spelling(type_node.text()));
            t.origin = ir::Origin::Declared;
            t.where = loc(type_node);
            v.type = t;
            const TypeName name = parse_type_name(t.base);
            if (!is_fundamental_name(name.name)) add_construction(name, Construction::Form::Brace, args, n);
        }
        return v;
    }

    ValueInfo new_expression(const Node& n) {
        const Node type_node = n.child_by_field("type");
        std::vector<ValueInfo> args;
        for (const Node& child : n.named_children()) {
            if (child.type() == "argument_list" || child.type() == "initializer_list") args = arguments(child);
        }
        ValueInfo v = value_of(n);
        if (type_node.is_null()) return v;

        add_type_uses(type_node);
        const std::string spelled = ir::normalize_spelling(type_node.text());
        const TypeName name = parse_type_name(spelled);
        if (!is_fundamental_name(name.name)) add_construction(name, Construction::Form::New, args, n);

        ir::Type t = ir::make_type(spelled, false, ir::RefKind::None, 1);
        t.origin = ir::Origin::Declared;
        t.where = loc(type_node);
        v.type = t;
        return v;
    }

    ValueInfo binary(const Node& n) {
        const std::string op(n.child_by_field("operator").text());
        ValueInfo a = expr(n.child_by_field("left"));
        ValueInfo b = expr(n.child_by_field("right"));
        ValueInfo v = value_of(n);

        if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
            compare(a, b, "a comparison");
            v.type = inferred_type("bool", "result of " + op);
        } else if (op == "&&" || op == "||") {
            apply_use(a, use_of(Use::Condition, bool_hint("operand of " + op)));
            apply_use(b, use_of(Use::Condition, bool_hint("operand of " + op)));
            v.type = inferred_type("bool", "result of " + op);
        } else if (op == "<<" || op == ">>") {
            // stream insertion or shift: nothing is implied about the operands
        }
        return v;
    }

    ValueInfo conditional(const Node& n) {
        const ValueInfo c = expr(n.child_by_field("condition"));
        apply_use(c, use_of(Use::Condition, bool_hint("condition of ?:")));
        ValueInfo a = expr(n.child_by_field("consequence"));
        ValueInfo b = expr(n.child_by_field("alternative"));
        ValueInfo v = value_of(n);
        if (a.type && b.type && a.type->spelling == b.type->spelling) v.type = a.type;
        return v;
    }

    ValueInfo assignment(const Node& n) {
        const Node left_node = n.child_by_field("left");
        const std::string op(n.child_by_field("operator").text());
        const ValueInfo left = expr(left_node);
        const ValueInfo right = expr(n.child_by_field("right"));

        if (op == "=") {
            // `obj.x = 5;` says what x is; `n = foo.count();` says what count returns.
            if (left.from.kind == FactRef::Kind::FieldAccess && right.type) {
                apply_use(left, use_of(Use::Assigned, right.type));
            } else if (right.is_string_literal && left.from.kind == FactRef::Kind::FieldAccess) {
                apply_use(left, use_of(Use::Assigned, inferred_type("std::string", "assigned a string literal")));
            }
            if (left.type) apply_use(right, use_of(Use::Assigned, left.type));
        }
        return value_of(n);
    }

    ValueInfo lambda(const Node& n) {
        symbols_.push();
        const Node declarator = n.child_by_field("declarator");
        declare_parameters(syntax::find_first(declarator, "parameter_list"));
        statement(n.child_by_field("body"));
        symbols_.pop();
        return value_of(n);
    }

    // `obj.field` and `obj->field`, not called.
    ValueInfo field_value(const Node& n) {
        const Node object = n.child_by_field("argument");
        const Node field = n.child_by_field("field");
        const bool arrow = syntax::has_token(n, "->");
        ValueInfo v = value_of(n);

        if (object.type() == "this") {
            if (const Symbol* s = symbols_.lookup(std::string(field.text()))) {
                v.is_lvalue = true;
                v.type = s->type;
            }
            return v;
        }
        const ValueInfo receiver = expr(object);
        if (const std::optional<Receiver> r = receiver_of(receiver, arrow)) {
            FieldAccess access;
            access.receiver = r->cls;
            access.field = std::string(field.text());
            access.on_const = r->on_const;
            access.via_arrow = arrow;
            access.where = loc(field);
            access.ns = ns_;
            facts_.field_accesses.push_back(std::move(access));
            v.from = {FactRef::Kind::FieldAccess, facts_.field_accesses.size() - 1};
        }
        return v;
    }

    ValueInfo call(const Node& n) {
        const Node fn = n.child_by_field("function");
        const Node arg_list = n.child_by_field("arguments");
        const std::string_view fn_type = fn.type();

        if (fn_type == "field_expression") return method_call(n, fn, arg_list);
        if (fn_type == "identifier" && classify_assertion(fn.text()) != Assert::None) {
            // An assertion nested in another (ASSERT_NO_THROW(EXPECT_EQ(a, b))) or
            // used as a value: handled like the statement form, never a free call.
            (void)assertion(n);
            return value_of(n);
        }
        if (fn_type == "identifier") return free_call(n, fn, arg_list);
        if (fn_type == "qualified_identifier") return scoped_call(n, fn, arg_list);
        if (fn_type == "template_function") return named_cast_or_template(n, fn, arg_list);
        if (fn_type == "primitive_type") {
            // size_t(2), int(x), uint8_t(1): tree-sitter makes the fixed-width and
            // size typedef names primitive types. A cast, and not a call to anything.
            (void)arguments(arg_list);
            ValueInfo v = value_of(n);
            ir::Type type = ir::make_type(fn.text());
            type.origin = ir::Origin::Declared;
            type.where = loc(fn);
            v.type = type;
            return v;
        }

        // (*callback)(x), lambda calls, f()(): evaluate for the calls inside.
        (void)expr(fn);
        (void)arguments(arg_list);
        return value_of(n);
    }

    ValueInfo free_call(const Node& n, const Node& fn, const Node& arg_list) {
        return free_call_with(n, fn, arguments(arg_list));
    }

    ValueInfo free_call_with(const Node& n, const Node& fn, std::vector<ValueInfo> args) {
        std::string name(fn.text());
        if (name.compare(0, 2, "::") == 0) name.erase(0, 2);   // ::helper(1) is helper(1) for our purposes
        ValueInfo v = value_of(n);

        if (name == "GetParam" && args.empty() && param_type_) {
            v.type = param_type_;
            return v;
        }
        if (const Symbol* s = symbols_.lookup(name)) {
            // `check(3)` where check is a lambda, a function pointer or a functor
            // the test declared: not a function of the code under test. If it is an
            // object of a class, it is a call to that class's operator().
            for (const ValueInfo& a : args) apply_use(a, use_of(Use::Argument));
            ValueInfo callee = value_of(fn);
            callee.type = s->type;
            if (const std::optional<Receiver> r = receiver_of(callee, false)) {
                MethodCall call;
                call.receiver = r->cls;
                call.method = "operator()";
                call.args = std::move(args);
                call.on_const = r->on_const;
                call.where = loc(fn);
                call.ns = ns_;
                facts_.method_calls.push_back(std::move(call));
                v.from = {FactRef::Kind::MethodCall, facts_.method_calls.size() - 1};
            }
            return v;
        }
        if (using_std_ && name == "move" && args.size() == 1) {
            ValueInfo moved = args[0];
            moved.is_moved = true;
            moved.text = std::string(n.text());
            return moved;
        }
        if (!expected_constructor_.empty() && name == expected_constructor_) {
            // `Foo g = Foo(3);`
            add_construction(parse_type_name(name), Construction::Form::Functional, std::move(args), n);
            ir::Type t = ir::make_type(name);
            t.origin = ir::Origin::Declared;
            t.where = loc(fn);
            v.type = t;
            return v;
        }
        for (const ValueInfo& a : args) apply_use(a, use_of(Use::Argument));

        FreeCall call;
        call.name = name;
        call.args = std::move(args);
        call.where = loc(fn);
        call.ns = ns_;
        call.owner = owner_ != nullptr ? owner_->name : std::string();
        facts_.free_calls.push_back(std::move(call));
        v.from = {FactRef::Kind::FreeCall, facts_.free_calls.size() - 1};
        return v;
    }

    ValueInfo scoped_call(const Node& n, const Node& fn, const Node& arg_list) {
        return scoped_call_with(n, fn, arguments(arg_list));
    }

    ValueInfo scoped_call_with(const Node& n, const Node& fn, std::vector<ValueInfo> args) {
        const TypeName name = parse_type_name(fn.text());
        ValueInfo v = value_of(n);
        if (name.scope.empty()) return free_call_with(n, fn, std::move(args));   // only a leading ::

        const bool std_scope = name.scope.size() == 1 && name.scope[0] == "std";
        if (std_scope && smart_pointer_factory(n, fn, name, args, v)) return v;
        if (std_scope && name.name == "move" && args.size() == 1) {
            ValueInfo moved = args[0];
            moved.is_moved = true;
            moved.text = std::string(n.text());
            return moved;
        }
        for (const ValueInfo& a : args) apply_use(a, use_of(Use::Argument));

        ScopedCall call;
        call.scope = name.scope;
        call.name = name.name;
        call.args = std::move(args);
        call.where = loc(fn);
        call.ns = ns_;
        facts_.scoped_calls.push_back(std::move(call));
        v.from = {FactRef::Kind::ScopedCall, facts_.scoped_calls.size() - 1};
        return v;
    }

    // std::make_unique<Foo>(2), also make_shared and the unqualified spelling:
    // Foo is constructed with (2), and the value is a smart pointer to it.
    bool smart_pointer_factory(const Node& n, const Node& fn, const TypeName& name, std::vector<ValueInfo>& args,
                               ValueInfo& v) {
        if ((name.name != "make_unique" && name.name != "make_shared") || name.template_args.empty()) return false;
        const std::string pointee = first_template_argument(name.template_args);
        const TypeName constructed = parse_type_name(pointee);
        add_type_use(constructed, fn);
        if (!is_fundamental_name(constructed.name)) {
            add_construction(constructed, Construction::Form::MakeUnique, std::move(args), n);
        }
        ir::Type t = ir::make_type("std::" + name.name.substr(5) + "_ptr<" + pointee + ">");
        t.origin = ir::Origin::Inferred;
        t.reason = "std::" + name.name;
        v.type = t;
        return true;
    }

    // static_cast<T>(e): a value of type T. make_unique<T>(...) without `std::`.
    // Any other function template is not followed.
    ValueInfo named_cast_or_template(const Node& n, const Node& fn, const Node& arg_list) {
        const std::string name(fn.child_by_field("name").text());
        std::vector<ValueInfo> args = arguments(arg_list);
        ValueInfo v = value_of(n);
        if (smart_pointer_factory(n, fn, parse_type_name(fn.text()), args, v)) return v;
        if (is_named_cast(name)) {
            const Node targs = fn.child_by_field("arguments");
            for (const Node& a : targs.named_children()) {
                if (a.type() != "type_descriptor") continue;
                add_type_uses(a);
                ir::Type t = syntax::make_declared_type(syntax::read_specifiers(a),
                                                        syntax::parse_declarator(a.child_by_field("declarator")), file_);
                if (!t.empty()) v.type = t;
            }
        } else {
            diagnostics_.warning(core::code::unsupported,
                                 "the call to the function template '" + std::string(fn.text()) + "' is not followed; "
                                 "template arguments of a function under test are not inferred",
                                 loc(fn));
        }
        return v;
    }

    ValueInfo method_call(const Node& n, const Node& fn, const Node& arg_list) {
        const Node object = fn.child_by_field("argument");
        const Node field = fn.child_by_field("field");
        const bool arrow = syntax::has_token(fn, "->");
        ValueInfo v = value_of(n);

        // this->helper(): a call on the fixture itself, indistinguishable here
        // from an unqualified call.
        if (object.type() == "this") {
            std::vector<ValueInfo> args = arguments(arg_list);
            for (const ValueInfo& a : args) apply_use(a, use_of(Use::Argument));
            FreeCall call;
            call.name = std::string(field.text());
            call.args = std::move(args);
            call.where = loc(field);
            call.ns = ns_;
            call.owner = owner_ != nullptr ? owner_->name : std::string();
            facts_.free_calls.push_back(std::move(call));
            v.from = {FactRef::Kind::FreeCall, facts_.free_calls.size() - 1};
            return v;
        }

        const ValueInfo receiver = expr(object);
        std::vector<ValueInfo> args = arguments(arg_list);
        for (const ValueInfo& a : args) apply_use(a, use_of(Use::Argument));

        const std::optional<Receiver> r = receiver_of(receiver, arrow);
        if (!r) {
            // foo.make().run(), or `auto x = foo.make(); x.run();`: what make()
            // returns is not known. The declaration of x claimed the result first
            // (AutoBound), so Receiver only lands on the direct form.
            const FactRef root = receiver.from.valid() ? receiver.from : receiver.auto_origin;
            if (root.valid()) {
                ValueInfo held = receiver;
                held.from = root;
                apply_use(held, use_of(Use::Receiver));
                // Every link is kept: the first hangs from a call, the next from the
                // one before it. Only the first is a gap the user sees (the others
                // have no result of their own to complain about), but once the
                // first is resolved the second can be, and so on.
                Unresolved u;
                u.kind = Unresolved::Kind::ChainedCall;
                u.text = std::string(n.text());
                u.where = loc(n);
                u.root = root;
                u.call.method = std::string(field.text());
                u.call.args = std::move(args);
                u.call.via_arrow = arrow;
                u.call.where = loc(field);
                u.call.ns = ns_;
                facts_.unresolved.push_back(std::move(u));
                v.from = {FactRef::Kind::Unresolved, facts_.unresolved.size() - 1};
            } else if (object.type() == "identifier" && !receiver.is_lvalue) {
                diagnostics_.note(core::code::unknown_receiver,
                                  "cannot tell what '" + receiver.text + "' is, so the call to " +
                                      std::string(field.text()) + " is not seen",
                                  loc(object));
            } else if (receiver.is_lvalue && !receiver.type) {
                diagnostics_.note(core::code::unknown_receiver,
                                  "'" + receiver.text + "' is declared with a type that is not known, so the call to " +
                                      std::string(field.text()) + " is not seen",
                                  loc(object));
            }
            return v;
        }

        MethodCall call;
        call.receiver = r->cls;
        call.method = std::string(field.text());
        call.args = std::move(args);
        call.on_const = r->on_const;
        call.via_arrow = arrow;
        call.where = loc(field);
        call.ns = ns_;
        facts_.method_calls.push_back(std::move(call));
        v.from = {FactRef::Kind::MethodCall, facts_.method_calls.size() - 1};
        return v;
    }

    const syntax::Tree&          tree_;
    const std::string&           file_;
    const TestFrontendOptions&   options_;
    TestFileFacts&               facts_;
    core::Diagnostics&           diagnostics_;

    SymbolTable                  symbols_;
    std::vector<std::string>     ns_;
    const ir::Class*             owner_ = nullptr;
    std::optional<ir::Type>      param_type_;
    std::string                  expected_constructor_;
    std::vector<std::string>     template_params_;   // names of the template parameters in scope
    bool                         using_std_ = false; // `using namespace std;` somewhere
};

}  // namespace

TestFileFacts read_test_file(const core::SourceFile& source, const TestFrontendOptions& options,
                             core::Diagnostics& diagnostics) {
    const syntax::Preprocessed pre = syntax::preprocess(source.text, options.macros);
    const syntax::Tree tree = syntax::parse_cpp(pre.text);

    TestFileFacts facts;
    facts.file = source.path;
    // The file's own classes, and its includes, through the code frontend. It
    // also reports the parse errors, once.
    facts.local = [&] {
        ir::Model model;
        read_declarations(tree, pre.markers, source.path, std::string(), model, diagnostics);
        return model;
    }();
    facts.includes = facts.local.includes;
    facts.annotations = read_annotations(tree, source.path, diagnostics);

    Collector(tree, source.path, options, facts, diagnostics).run();
    return facts;
}

}  // namespace mt::frontend
