// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/frontend/inference.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <tuple>

#include "mirrortest/frontend/known_names.h"
#include "mirrortest/ir/type.h"

namespace mt::frontend {

namespace {

bool before(const ir::SourceLoc& a, const ir::SourceLoc& b) {
    return std::tie(a.line, a.column) < std::tie(b.line, b.column);
}

std::string where_text(const ir::SourceLoc& w) {
    if (!w.known()) return w.file;
    return w.file + ":" + std::to_string(w.line);
}

bool starts_with(std::string_view s, std::string_view prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

// ---- spellings -------------------------------------------------------------------------------------------

// The words of a fundamental type in one canonical spelling, so that
// `unsigned` and `unsigned int`, or `long int` and `long`, are recognised as
// the same thing when two uses are compared. Anything else comes back as it is.
std::string canonical_base(std::string_view base) {
    std::string text = ir::normalize_spelling(base);
    if (starts_with(text, "::")) text.erase(0, 2);

    std::istringstream in(text);
    std::string word;
    int longs = 0;
    bool is_signed = false, is_unsigned = false, is_short = false, is_char = false, is_int = false, is_double = false;
    std::string other;
    std::size_t count = 0;
    while (in >> word) {
        ++count;
        if (word == "long") ++longs;
        else if (word == "signed") is_signed = true;
        else if (word == "unsigned") is_unsigned = true;
        else if (word == "short") is_short = true;
        else if (word == "char") is_char = true;
        else if (word == "int") is_int = true;
        else if (word == "double") is_double = true;
        else other = word;
    }
    if (!other.empty() || count == 0) {
        // `std::size_t` and `size_t` are one type to the reader.
        if (starts_with(text, "std::") && is_standard_c_type(std::string_view(text).substr(5))) return text.substr(5);
        return text;
    }
    if (is_double) return longs > 0 ? "long double" : "double";
    if (is_char) return is_unsigned ? "unsigned char" : is_signed ? "signed char" : "char";
    const std::string sign = is_unsigned ? "unsigned " : "";
    if (is_short) return sign + "short";
    if (longs >= 2) return sign + "long long";
    if (longs == 1) return sign + "long";
    if (is_int || is_signed || is_unsigned) return sign + "int";
    return text;   // float, bool, void, wchar_t, ...
}

bool is_exotic(const ir::Type& t) {
    return t.base.find('(') != std::string::npos || t.base.find('[') != std::string::npos;
}

// A key that two types share when they are the same type written differently.
std::string canonical_key(const ir::Type& t) {
    if (is_exotic(t)) return t.spelling;
    std::string key;
    if (t.is_const) key += "const ";
    key += canonical_base(t.base);
    key.append(t.pointer_depth, '*');
    if (t.ref == ir::RefKind::LValue) key += "&";
    else if (t.ref == ir::RefKind::RValue) key += "&&";
    return key;
}

ir::Type with_shape(const ir::Type& t, bool is_const, ir::RefKind ref, unsigned depth) {
    ir::Type out = ir::make_type(t.base, is_const, ref, depth);
    out.origin = t.origin;
    out.reason = t.reason;
    out.where = t.where;
    return out;
}

// The type of a value stored or passed by value: no reference, no top-level const.
ir::Type value_type(const ir::Type& t) {
    if (is_exotic(t) || t.empty()) return t;
    return with_shape(t, t.pointer_depth > 0 ? t.is_const : false, ir::RefKind::None, t.pointer_depth);
}

// What a function must return for `T v = f();` to be written with this T: a
// value, unless T is a non-const lvalue reference -- a temporary would not bind.
ir::Type return_type_for(const ir::Type& declared) {
    if (is_exotic(declared) || declared.empty()) return declared;
    if (declared.ref == ir::RefKind::LValue && !declared.is_const) return declared;
    return value_type(declared);
}

// ---- evidence ------------------------------------------------------------------------------------------------

struct Evidence {
    ir::Type                    type;
    ir::SourceLoc               where;       // of the thing that said it, for notes and conflicts
    std::string                 reason;
    std::vector<LiteralWarning> warnings;    // reported only if this evidence wins
    bool                        moved = false;

    ir::Origin origin() const { return type.origin; }
};

// One call, whichever kind of fact it came from.
struct Call {
    FactRef                        ref;
    const std::vector<ValueInfo>*  args = nullptr;
    const ResultUse*               use = nullptr;
    const CallAnnotation*          annotation = nullptr;
    ir::SourceLoc                  where;
    std::string                    name;
    bool                           is_static = false;
    bool                           on_const = false;
};

class Inference {
public:
    Inference(const Classified& classified, const core::Config& config, const std::vector<ir::Model>& known,
              core::Diagnostics& diagnostics)
        : c_(classified), config_(config), diag_(diagnostics) {
        for (const ir::Enum& e : c_.facts.local.enums) enums_.insert(e.name);
        for (const ir::Model& m : known) {
            for (const ir::Enum& e : m.enums) enums_.insert(e.name);
        }
        for (const std::string& t : config_.by_value_types) by_value_.insert(canonical_base(t));
        for (const auto& entry : config_.type_map) type_map_[canonical_base(entry.first)] = entry.second;
    }

    ir::Model run() {
        ir::Model model;
        for (const ClassTarget& target : c_.classes) model.classes.push_back(build_class(target));
        for (const FunctionTarget& target : c_.functions) build_functions(target, model);
        check_standard(model);
        return model;
    }

private:
    // ---- types of values -----------------------------------------------------------------------------------

    bool is_by_value(const ir::Type& t) const {
        if (t.pointer_depth > 0 || is_exotic(t)) return true;
        std::string base = canonical_base(t.base);
        if (by_value_.count(base) != 0) return true;
        if (starts_with(base, "std::")) base.erase(0, 5);
        if (by_value_.count(base) != 0) return true;

        static const char* const fundamental[] = {
            "bool", "char", "signed char", "unsigned char", "short", "unsigned short", "int", "unsigned int", "long",
            "unsigned long", "long long", "unsigned long long", "float", "double", "long double", "wchar_t",
            "char8_t", "char16_t", "char32_t", "void",
            "size_t", "ptrdiff_t", "nullptr_t", "byte", "intptr_t", "uintptr_t", "intmax_t", "uintmax_t", "ssize_t",
            "string_view", "wstring_view", "QStringView", "qreal", "qsizetype", "qptrdiff", "qintptr", "quintptr",
            "uchar", "ushort", "uint", "ulong", "qlonglong", "qulonglong",
        };
        for (const char* name : fundamental) {
            if (base == name) return true;
        }
        if (is_fixed_width(base) || starts_with(base, "chrono::")) return true;
        return enums_.count(base) != 0;
    }

    // int32_t, uint_fast16_t, int_least8_t, qint64, quint8
    static bool is_fixed_width(std::string_view s) {
        if (starts_with(s, "qint") || starts_with(s, "quint")) {
            const std::size_t digits = s.find_first_of("0123456789");
            return digits != std::string_view::npos &&
                   std::all_of(s.begin() + static_cast<std::ptrdiff_t>(digits), s.end(),
                               [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; });
        }
        if (starts_with(s, "u")) s.remove_prefix(1);
        if (!starts_with(s, "int")) return false;
        s.remove_prefix(3);
        if (starts_with(s, "_fast")) s.remove_prefix(5);
        else if (starts_with(s, "_least")) s.remove_prefix(6);
        if (s.size() < 3 || s.substr(s.size() - 2) != "_t") return false;
        const std::string_view digits = s.substr(0, s.size() - 2);
        return !digits.empty() && std::all_of(digits.begin(), digits.end(),
                                              [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; });
    }

    // The type of a parameter that receives this argument (design 7.9).
    std::optional<Evidence> param_evidence(const ValueInfo& v) const {
        if (v.is_null_pointer) return std::nullopt;   // a pointer of unknown type
        Evidence e;
        e.where = v.where;
        e.warnings = v.warnings;

        if (v.is_string_literal) {
            e.type = ir::make_type(config_.string_literal_param);
            e.type.origin = ir::Origin::Inferred;
            e.type.reason = "string literal " + v.text;
            e.type.where = v.where;
            e.reason = e.type.reason;
            return e;
        }
        if (!v.type) {
            // The result of another call of the API: known only if it was annotated
            // (propagating inferred results is task 2.8).
            if (const std::optional<ir::Type> returned = annotated_result(v.from)) {
                e.type = return_type_for(*returned);
                e.reason = "the annotated result of " + v.text;
                e.type.reason = e.reason;
                return e;
            }
            return std::nullopt;
        }

        ir::Type t = *v.type;
        if (t.empty() || t.base.find('[') != std::string::npos) return std::nullopt;   // arrays decay in ways the text does not show
        e.reason = t.reason;

        if (v.is_address_of) {
            e.type = t;                          // already the pointer
        } else if (v.is_moved) {
            e.type = value_type(t);              // handed over: taken by value
            e.moved = true;
        } else if (v.is_lvalue && !v.var_name.empty()) {
            const ir::Type plain = value_type(t);
            e.type = is_by_value(plain) ? plain : with_shape(plain, true, ir::RefKind::LValue, 0);
        } else {
            e.type = value_type(t);
        }
        if (e.reason.empty()) e.reason = "'" + v.text + "'";
        if (e.type.reason.empty()) e.type.reason = e.reason;
        return e;
    }

    std::optional<ir::Type> annotated_result(const FactRef& ref) const {
        const CallAnnotation* a = annotation_at(ref);
        if (a != nullptr && a->returns) return a->returns;
        return std::nullopt;
    }

    const CallAnnotation* annotation_at(const FactRef& ref) const {
        const TestFileFacts& f = c_.facts;
        switch (ref.kind) {
            case FactRef::Kind::MethodCall:  return &f.method_calls[ref.index].annotation;
            case FactRef::Kind::ScopedCall:  return &f.scoped_calls[ref.index].annotation;
            case FactRef::Kind::FreeCall:    return &f.free_calls[ref.index].annotation;
            case FactRef::Kind::FieldAccess: return &f.field_accesses[ref.index].annotation;
            case FactRef::Kind::Unresolved:  return &f.unresolved[ref.index].call.annotation;
            case FactRef::Kind::None:        break;
        }
        return nullptr;
    }

    // ---- choosing among uses -------------------------------------------------------------------------------------------

    // The evidence with the highest Origin, the first in source order among
    // equals. Different answers are not hidden: `mt-conflict` lists them.
    const Evidence* choose(const std::vector<Evidence>& evidence, const std::string& what) {
        if (evidence.empty()) return nullptr;
        const Evidence* best = &evidence.front();
        for (const Evidence& e : evidence) {
            if (e.origin() > best->origin()) best = &e;
        }
        std::string others;
        for (const Evidence& e : evidence) {
            if (&e == best || canonical_key(e.type) == canonical_key(best->type)) continue;
            others += (others.empty() ? "" : ", ") + std::string("'") + e.type.spelling + "' (" + e.reason + ", " +
                      where_text(e.where) + ")";
        }
        if (!others.empty()) {
            diag_.warning(core::code::conflict,
                          what + " is inferred as '" + best->type.spelling + "' (" + best->reason + ", " +
                              where_text(best->where) + ") but also as " + others + "; using the first",
                          best->where);
        }
        for (const LiteralWarning& w : best->warnings) diag_.warning(w.code, w.message, best->where);
        return best;
    }

    // ---- what nobody says (D5) ------------------------------------------------------------------------------------------------

    ir::Type placeholder(const std::string& reason, const ir::SourceLoc& where) const {
        ir::Type t = ir::make_type(config_.placeholder_type);
        t.origin = ir::Origin::Defaulted;
        t.reason = reason;
        t.where = where;
        return t;
    }

    // An error under `auto_policy: error`; under `todo`, a warning, a placeholder
    // type and a note saying the type is a guess.
    ir::Type unknown(const char* code, const std::string& message, const std::string& subject, const ir::SourceLoc& where,
                     std::vector<std::string>& notes) {
        if (config_.auto_policy == core::AutoPolicy::Error) {
            diag_.error(code, message, where);
        } else {
            diag_.warning(code, message + "; assuming '" + config_.placeholder_type + "'", where);
        }
        notes.push_back(subject + " is unknown; assumed '" + config_.placeholder_type + "' (" + where_text(where) + ")");
        return placeholder(subject + " unknown", where);
    }

    static void note_inferred(std::vector<std::string>& notes, const std::string& what, const Evidence& e) {
        if (e.origin() == ir::Origin::Inferred || e.origin() == ir::Origin::Defaulted) {
            notes.push_back(what + " inferred from " + e.reason + " (" + where_text(e.where) + ")");
        }
    }

    ir::Type mapped(ir::Type t) const {
        if (t.origin > ir::Origin::Inferred || is_exotic(t) || t.empty()) return t;
        const auto found = type_map_.find(canonical_base(t.base));
        if (found == type_map_.end()) return t;
        ir::Type out = ir::make_type(found->second, t.is_const, t.ref, t.pointer_depth);
        out.origin = t.origin;
        out.reason = t.reason + " (type_map)";
        out.where = t.where;
        return out;
    }

    // ---- views of calls ----------------------------------------------------------------------------------------------------------

    Call view_method(std::size_t i, bool is_static) const {
        const MethodCall& m = c_.facts.method_calls[i];
        return {{FactRef::Kind::MethodCall, i}, &m.args, &m.use, &m.annotation, m.where, m.method, is_static, m.on_const};
    }
    Call view_scoped(std::size_t i, bool is_static) const {
        const ScopedCall& s = c_.facts.scoped_calls[i];
        return {{FactRef::Kind::ScopedCall, i}, &s.args, &s.use, &s.annotation, s.where, s.name, is_static, false};
    }
    Call view_free(std::size_t i) const {
        const FreeCall& f = c_.facts.free_calls[i];
        return {{FactRef::Kind::FreeCall, i}, &f.args, &f.use, &f.annotation, f.where, f.name, false, false};
    }

    static void sort_by_position(std::vector<Call>& calls) {
        std::stable_sort(calls.begin(), calls.end(), [](const Call& a, const Call& b) { return before(a.where, b.where); });
    }

    // ---- overrides (config + annotations) ------------------------------------------------------------------------------------------

    core::MemberOverride override_for(const std::vector<std::string>& ns, const std::string& klass, const std::string& member) const {
        core::MemberOverride out;
        const auto overlay = [&](const std::string& key) {
            const auto found = c_.overrides.find(key);
            if (found == c_.overrides.end()) return;
            const core::MemberOverride& o = found->second;
            if (o.returns) out.returns = o.returns;
            for (const auto& p : o.params) out.params[p.first] = p.second;
            if (o.is_const) out.is_const = o.is_const;
            if (o.is_static) out.is_static = o.is_static;
            if (o.is_virtual) out.is_virtual = o.is_virtual;
            if (o.is_noexcept) out.is_noexcept = o.is_noexcept;
        };
        if (!klass.empty()) overlay(klass + "::" + member);                       // Class::member, then the more specific
        overlay(ir::qualified_name(ns, klass.empty() ? member : klass + "::" + member));
        return out;
    }

    static Evidence annotated(const std::string& type_text, const ir::SourceLoc& where, const char* what) {
        Evidence e;
        e.type = ir::make_type(type_text);
        e.type.origin = ir::Origin::Annotated;
        e.type.reason = what;
        e.type.where = where;
        e.where = where;
        e.reason = what;
        return e;
    }

    // ---- parameters ----------------------------------------------------------------------------------------------------------------------

    std::vector<ir::Param> infer_params(const std::string& subject, std::size_t arity, const std::vector<const Call*>& calls,
                                        const core::MemberOverride& over, std::vector<std::string>& notes) {
        std::vector<ir::Param> params;
        for (std::size_t i = 0; i < arity; ++i) {
            const std::string what = "parameter " + std::to_string(i + 1) + " of '" + subject + "'";
            std::vector<Evidence> evidence;
            const auto from_override = over.params.find(static_cast<unsigned>(i + 1));
            if (from_override != over.params.end()) {
                evidence.push_back(annotated(from_override->second, calls.front()->where, "annotation"));
            }
            for (const Call* call : calls) {
                if (const std::optional<Evidence> e = param_evidence((*call->args)[i])) evidence.push_back(*e);
            }

            ir::Param p;
            if (const Evidence* best = choose(evidence, what)) {
                p.type = best->type;
                note_inferred(notes, what, *best);
                if (best->moved) notes.push_back(what + " is taken by value because the tests move into it");
            } else {
                const ValueInfo& first = (*calls.front()->args)[i];
                p.type = unknown(core::code::unknown_param,
                                 "the type of " + what + " cannot be inferred (argument '" + first.text + "'); "
                                 "give the argument a typed variable or annotate it: Class::member param " +
                                     std::to_string(i + 1) + " TYPE",
                                 what, first.where.known() ? first.where : calls.front()->where, notes);
            }
            p.type = mapped(p.type);

            for (const Call* call : calls) {
                const std::string& name = (*call->args)[i].var_name;
                if (!name.empty()) {
                    p.name = name;
                    break;
                }
            }
            if (p.name.empty()) p.name = "arg" + std::to_string(i + 1);
            params.push_back(std::move(p));
        }
        unique_names(params);
        return params;
    }

    // `f(x, x)` and `f(a, 1)` with another `f(1, a)`: two parameters, one name.
    static void unique_names(std::vector<ir::Param>& params) {
        std::set<std::string> taken;
        for (ir::Param& p : params) {
            std::string name = p.name;
            for (int n = 2; !taken.insert(name).second; ++n) name = p.name + std::to_string(n);
            p.name = name;
        }
    }

    // ---- return types ----------------------------------------------------------------------------------------------------------------------

    struct Returned {
        ir::Type                 type;
        std::vector<std::string> notes;
        bool                     had_evidence = false;
    };

    Returned infer_return(const std::string& subject, const std::vector<const Call*>& calls,
                          const core::MemberOverride& over, bool is_function_like) {
        Returned out;
        const std::string what = "the return type of '" + subject + "'";
        std::vector<Evidence> evidence;
        if (over.returns) evidence.push_back(annotated(*over.returns, calls.front()->where, "annotation"));

        for (const Call* call : calls) {
            if (call->annotation->returns) {
                Evidence e;
                e.type = return_type_for(*call->annotation->returns);
                e.where = call->annotation->returns->where;
                e.reason = "annotation";
                evidence.push_back(e);
                continue;
            }
            const ResultUse& use = *call->use;
            const bool carries_hint = use.kind == UseKind::Compared || use.kind == UseKind::Condition ||
                                      use.kind == UseKind::Assigned || use.kind == UseKind::AutoBound;
            if (carries_hint && use.hint && !use.hint->empty()) {
                Evidence e;
                e.type = return_type_for(*use.hint);
                e.where = use.hint->where.known() ? use.hint->where : call->where;
                e.reason = use.hint->reason.empty() ? std::string("its use") : use.hint->reason;
                e.warnings = use.hint_warnings;
                evidence.push_back(e);
            }
        }

        if (const Evidence* best = choose(evidence, what)) {
            out.type = mapped(best->type);
            out.had_evidence = true;
            note_inferred(out.notes, "return type", *best);
            return out;
        }

        const ir::SourceLoc& first = calls.front()->where;
        const bool all_discarded = std::all_of(calls.begin(), calls.end(), [](const Call* c) { return c->use->kind == UseKind::Discarded; });
        const auto any_use = [&](UseKind kind) {
            return std::any_of(calls.begin(), calls.end(), [kind](const Call* c) { return c->use->kind == kind; });
        };

        if (all_discarded) {
            out.type = ir::make_type("void");
            out.type.origin = ir::Origin::Defaulted;
            out.type.reason = "result never used";
            out.type.where = first;
            out.notes.push_back("result never used; assumed void (" + where_text(first) + ")");
            diag_.warning(core::code::unused_result,
                          "the result of '" + subject + "' is never used, so " + (is_function_like ? "it" : "the method") +
                              " is assumed to return void",
                          first);
        } else if (any_use(UseKind::Receiver)) {
            out.type = unknown(core::code::chained_call,
                               "a call is made on the result of '" + subject + "', whose type nothing says; annotate it: "
                               "'// @mt: returns TYPE' on the statement, or Class::member returns TYPE",
                               "return type", first, out.notes);
        } else if (any_use(UseKind::AutoBound)) {
            out.type = unknown(core::code::auto_deduced,
                               "the result of '" + subject + "' is stored in an 'auto' variable, so its type is not known; "
                               "declare the variable's type, or annotate the call: '// @mt: returns TYPE'",
                               "return type", first, out.notes);
        } else {
            out.type = unknown(core::code::unknown_return,
                               what + " cannot be inferred: no test compares the result with a typed value; "
                               "annotate it: '// @mt: returns TYPE'",
                               "return type", first, out.notes);
        }
        out.type = mapped(out.type);
        return out;
    }

    // ---- classes -------------------------------------------------------------------------------------------------------------------------------

    struct Group {
        std::string               name;
        std::size_t               arity = 0;
        std::string               overload_key;     // the argument types, for calls annotated `overload`
        std::vector<const Call*>  calls;
    };

    // The type of each argument as it would be a parameter, as one string.
    std::string signature_of(const Call& call) const {
        std::string out;
        for (const ValueInfo& a : *call.args) {
            const std::optional<Evidence> e = param_evidence(a);
            out += (e ? canonical_key(e->type) : std::string("?")) + ",";
        }
        return out;
    }

    ir::Class build_class(const ClassTarget& target) {
        ir::Class cls;
        cls.name = target.name;
        cls.ns = target.ns;
        cls.header = target.header;
        cls.origin = ir::Origin::Inferred;
        cls.where = target.first_use;

        if (!target.instantiations.empty()) {
            diag_.warning(core::code::unsupported,
                          "'" + target.qualified() + "' is used as a template (" + target.name + "<" + target.instantiations.front() +
                              ">); class templates are not generated, so a plain class is, and the tests will not compile "
                              "until you declare the template yourself",
                          target.first_use);
        }

        build_constructors(target, cls);
        build_methods(target, cls);
        build_fields(target, cls);

        cls.is_struct = cls.methods.empty() && !cls.fields.empty();
        return cls;
    }

    // One constructor per arity used. The default one only if another exists too:
    // otherwise the implicit one is enough. Copying and moving a Foo is implicit.
    void build_constructors(const ClassTarget& target, ir::Class& cls) {
        struct Use {
            std::size_t              arity;
            std::vector<const Construction*> uses;
            bool                     copy_init = false;
            ir::SourceLoc            first;
        };
        std::vector<Use> by_arity;
        std::vector<const Construction*> all;
        for (std::size_t i : target.constructions) all.push_back(&c_.facts.constructions[i]);
        std::stable_sort(all.begin(), all.end(),
                         [](const Construction* a, const Construction* b) { return before(a->where, b->where); });

        for (const Construction* k : all) {
            if (k->args.size() == 1 && names_class(k->args.front(), target)) continue;   // copy or move
            auto found = std::find_if(by_arity.begin(), by_arity.end(), [&](const Use& u) { return u.arity == k->args.size(); });
            if (found == by_arity.end()) {
                by_arity.push_back({k->args.size(), {}, false, k->where});
                found = by_arity.end() - 1;
            }
            found->uses.push_back(k);
            if (k->form == Construction::Form::Copy) found->copy_init = true;
        }

        const bool others = std::any_of(by_arity.begin(), by_arity.end(), [](const Use& u) { return u.arity > 0; });
        for (const Use& u : by_arity) {
            if (u.arity == 0 && !others) continue;

            // The arguments live in the constructions; Call is the same shape as for methods.
            std::vector<Call> views;
            for (const Construction* k : u.uses) {
                Call call;
                call.args = &k->args;
                call.where = k->where;
                call.name = target.name;
                views.push_back(call);
            }
            std::vector<const Call*> calls;
            for (const Call& v : views) calls.push_back(&v);

            ir::Method m;
            m.name = target.name;
            m.kind = ir::MethodKind::Constructor;
            m.origin = ir::Origin::Inferred;
            m.where = u.first;
            const core::MemberOverride over = override_for(target.ns, target.name, target.name);
            m.params = infer_params(target.qualified() + "::" + target.name, u.arity, calls, over, m.notes);
            m.is_explicit = u.arity == 1 && !u.copy_init;
            m.is_noexcept = over.is_noexcept.value_or(false);
            cls.methods.push_back(std::move(m));
        }
    }

    static bool names_class(const ValueInfo& v, const ClassTarget& target) {
        if (!v.type) return false;
        std::string base = canonical_base(v.type->base);
        const std::size_t sep = base.rfind("::");
        if (sep != std::string::npos) base.erase(0, sep + 2);
        return base == target.name;
    }

    void build_methods(const ClassTarget& target, ir::Class& cls) {
        std::vector<Call> calls;
        for (std::size_t i : target.method_calls) calls.push_back(view_method(i, false));
        for (std::size_t i : target.static_calls) calls.push_back(view_scoped(i, true));
        calls.erase(std::remove_if(calls.begin(), calls.end(), [](const Call& c) { return c.annotation->ignore; }), calls.end());
        sort_by_position(calls);

        std::vector<Group> groups;
        for (const Call& call : calls) {
            const std::string key = call.annotation->overload ? signature_of(call) : std::string();
            auto found = std::find_if(groups.begin(), groups.end(), [&](const Group& g) {
                return g.name == call.name && g.arity == call.args->size() && g.overload_key == key;
            });
            if (found == groups.end()) {
                groups.push_back({call.name, call.args->size(), key, {}});
                found = groups.end() - 1;
            }
            found->calls.push_back(&call);
        }

        for (const Group& g : groups) {
            const std::string subject = target.qualified() + "::" + g.name;
            const core::MemberOverride over = override_for(target.ns, target.name, g.name);

            ir::Method m;
            m.name = g.name;
            m.kind = starts_with(g.name, "operator") ? ir::MethodKind::Operator : ir::MethodKind::Normal;
            m.origin = ir::Origin::Inferred;
            m.where = g.calls.front()->where;

            m.is_static = over.is_static.value_or(std::any_of(g.calls.begin(), g.calls.end(), [](const Call* c) { return c->is_static; }));
            const bool used_on_const = std::any_of(g.calls.begin(), g.calls.end(), [](const Call* c) { return !c->is_static && c->on_const; });
            m.is_const = !m.is_static && over.is_const.value_or(used_on_const);
            m.is_virtual = over.is_virtual.value_or(false);
            m.is_noexcept = over.is_noexcept.value_or(false);

            m.params = infer_params(subject, g.arity, g.calls, over, m.notes);
            Returned r = infer_return(subject, g.calls, over, false);
            m.ret = r.type;
            for (std::string& n : r.notes) m.notes.push_back(std::move(n));

            // Nothing is invented, but a getter that is only ever called through
            // a non-const object is worth a second look.
            const bool compared = std::any_of(g.calls.begin(), g.calls.end(), [](const Call* c) {
                return c->use->kind == UseKind::Compared || c->use->kind == UseKind::Condition;
            });
            if (!m.is_const && !m.is_static && g.arity == 0 && compared && !over.is_const.has_value()) {
                m.notes.push_back("consider making this const");
            }
            cls.methods.push_back(std::move(m));
        }
    }

    void build_fields(const ClassTarget& target, ir::Class& cls) {
        struct FieldUses {
            std::string                   name;
            std::vector<const FieldAccess*> accesses;
        };
        std::vector<FieldUses> fields;
        std::vector<const FieldAccess*> all;
        for (std::size_t i : target.field_accesses) all.push_back(&c_.facts.field_accesses[i]);
        std::stable_sort(all.begin(), all.end(),
                         [](const FieldAccess* a, const FieldAccess* b) { return before(a->where, b->where); });
        for (const FieldAccess* a : all) {
            if (a->annotation.ignore) continue;
            auto found = std::find_if(fields.begin(), fields.end(), [&](const FieldUses& f) { return f.name == a->field; });
            if (found == fields.end()) {
                fields.push_back({a->field, {}});
                found = fields.end() - 1;
            }
            found->accesses.push_back(a);
        }

        for (const FieldUses& f : fields) {
            const std::string subject = target.qualified() + "::" + f.name;
            const core::MemberOverride over = override_for(target.ns, target.name, f.name);

            std::vector<Call> views;
            for (const FieldAccess* a : f.accesses) {
                Call call;
                call.use = &a->use;
                call.annotation = &a->annotation;
                call.where = a->where;
                call.name = f.name;
                views.push_back(call);
            }
            std::vector<const Call*> calls;
            for (const Call& v : views) calls.push_back(&v);

            ir::Field field;
            field.name = f.name;
            field.origin = ir::Origin::Inferred;
            field.where = calls.front()->where;
            Returned r = infer_return(subject, calls, over, false);
            field.type = value_type(r.type);
            cls.fields.push_back(std::move(field));
        }
    }

    // ---- free functions -----------------------------------------------------------------------------------------------------------------

    void build_functions(const FunctionTarget& target, ir::Model& model) {
        std::vector<Call> calls;
        for (std::size_t i : target.free_calls) calls.push_back(view_free(i));
        for (std::size_t i : target.scoped_calls) calls.push_back(view_scoped(i, false));
        calls.erase(std::remove_if(calls.begin(), calls.end(), [](const Call& c) { return c.annotation->ignore; }), calls.end());
        sort_by_position(calls);

        std::vector<Group> groups;
        for (const Call& call : calls) {
            const std::string key = call.annotation->overload ? signature_of(call) : std::string();
            auto found = std::find_if(groups.begin(), groups.end(),
                                      [&](const Group& g) { return g.arity == call.args->size() && g.overload_key == key; });
            if (found == groups.end()) {
                groups.push_back({target.name, call.args->size(), key, {}});
                found = groups.end() - 1;
            }
            found->calls.push_back(&call);
        }

        for (const Group& g : groups) {
            const std::string subject = target.qualified();
            const core::MemberOverride over = override_for(target.ns, std::string(), target.name);

            ir::Function fn;
            fn.name = target.name;
            fn.ns = target.ns;
            fn.origin = ir::Origin::Inferred;
            fn.where = g.calls.front()->where;
            fn.is_noexcept = over.is_noexcept.value_or(false);
            fn.params = infer_params(subject, g.arity, g.calls, over, fn.notes);
            Returned r = infer_return(subject, g.calls, over, true);
            fn.ret = r.type;
            for (std::string& n : r.notes) fn.notes.push_back(std::move(n));
            model.functions.push_back(std::move(fn));
        }
    }

    // ---- --std ---------------------------------------------------------------------------------------------------------------------------------

    void check_standard(const ir::Model& model) {
        if (config_.cxx_std >= core::CxxStd::Cxx17) return;
        const auto check = [&](const ir::Type& t, const std::string& where_name) {
            for (const char* name : {"std::optional", "std::variant", "std::string_view", "std::byte"}) {
                if (t.spelling.find(name) != std::string::npos) {
                    diag_.warning(core::code::std_version,
                                  std::string("'") + name + "' needs C++17 and --std is " + core::to_string(config_.cxx_std) +
                                      " (in " + where_name + ")",
                                  t.where);
                }
            }
        };
        for (const ir::Class& c : model.classes) {
            for (const ir::Method& m : c.methods) {
                check(m.ret, c.name + "::" + m.name);
                for (const ir::Param& p : m.params) check(p.type, c.name + "::" + m.name);
            }
            for (const ir::Field& f : c.fields) check(f.type, c.name + "::" + f.name);
        }
        for (const ir::Function& f : model.functions) {
            check(f.ret, f.name);
            for (const ir::Param& p : f.params) check(p.type, f.name);
        }
    }

    const Classified&              c_;
    const core::Config&            config_;
    core::Diagnostics&             diag_;
    std::set<std::string>          enums_;
    std::set<std::string>          by_value_;
    std::map<std::string, std::string> type_map_;
};

}  // namespace

ir::Model infer(const Classified& classified, const core::Config& config, const std::vector<ir::Model>& known,
                core::Diagnostics& diagnostics) {
    return Inference(classified, config, known, diagnostics).run();
}

}  // namespace mt::frontend
