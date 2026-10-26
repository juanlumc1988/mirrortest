// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/frontend/annotations.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

#include "mirrortest/core/naming.h"
#include "mirrortest/frontend/classify.h"
#include "mirrortest/ir/type.h"
#include "mirrortest/syntax/extract.h"

namespace mt::frontend {

bool StatementRange::contains(const ir::SourceLoc& p) const {
    const auto key = [](const ir::SourceLoc& l) { return std::make_tuple(l.line, l.column); };
    return valid() && key(begin) <= key(p) && key(p) <= key(end);
}

namespace {

using syntax::Node;

std::string_view trim(std::string_view s) {
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && blank(s.front())) s.remove_prefix(1);
    while (!s.empty() && blank(s.back())) s.remove_suffix(1);
    return s;
}

// "returns  std::vector<int>" -> {"returns", "std::vector<int>"}
std::pair<std::string_view, std::string_view> take_word(std::string_view text) {
    text = trim(text);
    std::size_t end = 0;
    while (end < text.size() && text[end] != ' ' && text[end] != '\t') ++end;
    return {text.substr(0, end), trim(text.substr(end))};
}

// Splits at the ';' that are outside double quotes (a header path may hold one).
std::vector<std::string_view> split_directives(std::string_view text) {
    std::vector<std::string_view> out;
    bool quoted = false;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '"') {
            quoted = !quoted;
        } else if (text[i] == ';' && !quoted) {
            out.push_back(text.substr(begin, i - begin));
            begin = i + 1;
        }
    }
    out.push_back(text.substr(begin));
    return out;
}

bool is_all_digits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool is_block(std::string_view type) {
    return type == "compound_statement" || type == "translation_unit" || type == "declaration_list" ||
           type == "field_declaration_list" || type == "case_statement";
}

ir::SourceLoc end_of(const Node& n, const std::string& file) {
    ir::SourceLoc loc = syntax::location(n, file);
    for (char c : n.text()) {
        if (c == '\n') {
            ++loc.line;
            loc.column = 1;
        } else {
            ++loc.column;
        }
    }
    return loc;
}

StatementRange range_of(const Node& n, const std::string& file) {
    StatementRange r;
    r.begin = syntax::location(n, file);
    r.end = end_of(n, file);
    return r;
}

Node skip_comments_back(Node n) {
    while (!n.is_null() && n.type() == "comment") n = n.prev_named_sibling();
    return n;
}

Node skip_comments_forward(Node n) {
    while (!n.is_null() && n.type() == "comment") n = n.next_named_sibling();
    return n;
}

class Reader {
public:
    Reader(const std::string& file, core::Diagnostics& diagnostics) : file_(file), diag_(diagnostics) {}

    Annotations run(const syntax::Tree& tree) {
        syntax::walk(tree.root(), [&](const Node& n) {
            if (n.type() == "comment") comment(n);
            return true;
        });
        return std::move(out_);
    }

private:
    void fail(const std::string& message) { diag_.error(core::code::annotation, message, where_); }

    void comment(const Node& n) {
        const std::string_view text = n.text();
        where_ = syntax::location(n, file_);

        if (text.compare(0, 2, "/*") == 0) {
            if (trim(text.substr(2)).compare(0, 4, "@mt:") == 0) {
                fail("an annotation must be a line comment ('// @mt: ...'), not a block comment");
            }
            return;
        }
        if (text.compare(0, 2, "//") != 0) return;
        const std::string_view body = trim(text.substr(2));
        if (body.compare(0, 3, "@mt") != 0) return;
        if (body.compare(0, 4, "@mt:") != 0) {
            // `// @mt returns int`: not our prefix unless the word ends there.
            if (body.size() == 3 || body[3] == ' ' || body[3] == '\t') {
                fail("the annotation prefix is '// @mt:', with a colon");
            }
            return;
        }

        const std::string_view rest = trim(body.substr(4));
        if (rest.empty()) {
            fail("empty annotation: expected at least one directive after '@mt:'");
            return;
        }

        ShortAnnotation shorts;
        shorts.where = where_;
        bool any_short = false;
        for (const std::string_view directive_text : split_directives(rest)) {
            const std::string_view d = trim(directive_text);
            if (!d.empty()) directive(d, shorts, any_short);
        }
        if (any_short) {
            shorts.target = target_of(n);
            out_.shorts.push_back(std::move(shorts));
        }
    }

    void directive(std::string_view text, ShortAnnotation& shorts, bool& any_short) {
        const auto [word, rest] = take_word(text);
        if (word == "returns") {
            if (shorts.returns) {
                fail("'returns' appears twice in the same annotation");
            } else if (const std::optional<ir::Type> type = parse_type(rest, "returns")) {
                shorts.returns = type;
                any_short = true;
            }
        } else if (word == "overload" || word == "ignore") {
            if (!rest.empty()) {
                fail("unexpected text '" + std::string(rest) + "' after '" + std::string(word) + "'");
                return;
            }
            (word == "overload" ? shorts.overload : shorts.ignore) = true;
            any_short = true;
        } else if (word == "class") {
            class_directive(rest);
        } else if (word.find("::") != std::string_view::npos) {
            member_directive(word, rest);
        } else {
            fail("unknown annotation '" + std::string(word) + "'; expected returns, overload, ignore, "
                 "class NAME namespace|header ..., or Class::member returns|param|const|static|virtual|noexcept");
        }
    }

    std::optional<ir::Type> parse_type(std::string_view text, const char* directive) {
        text = trim(text);
        if (text.empty()) {
            fail(std::string("'") + directive + "' needs a type");
            return std::nullopt;
        }
        ir::Type type = ir::type_from_spelling(text);
        type.origin = ir::Origin::Annotated;
        type.reason = "annotation";
        type.where = where_;
        return type;
    }

    // Each long directive may appear once per member: two of them would leave it
    // to the reader to guess which wins.
    bool first_time(const std::string& key) {
        const auto inserted = seen_.emplace(key, where_);
        if (inserted.second) return true;
        fail("this was already annotated at line " + std::to_string(inserted.first->second.line));
        return false;
    }

    void class_directive(std::string_view rest) {
        const auto [name, after_name] = take_word(rest);
        const auto [keyword, value] = take_word(after_name);
        if (name.empty() || keyword.empty()) {
            fail("expected 'class NAME namespace NS' or 'class NAME header \"PATH\"'");
            return;
        }
        if (!core::is_identifier(name)) {
            fail("'" + std::string(name) + "' is not a class name; write it without a namespace");
            return;
        }
        if (keyword == "namespace") {
            if (!core::is_qualified_name(value, 1)) {
                fail("'" + std::string(value) + "' is not a namespace name; expected something like acme or acme::detail");
            } else if (first_time("class|" + std::string(name) + "|namespace")) {
                out_.class_namespace[std::string(name)] = std::string(value);
            }
        } else if (keyword == "header") {
            if (value.size() < 3 || value.front() != '"' || value.back() != '"' ||
                value.substr(1, value.size() - 2).find('"') != std::string_view::npos) {
                fail("the header path must be written in double quotes, e.g. class Foo header \"acme/foo.h\"");
            } else if (first_time("class|" + std::string(name) + "|header")) {
                out_.class_header[std::string(name)] = std::string(value.substr(1, value.size() - 2));
            }
        } else {
            fail("expected 'namespace' or 'header' after the class name, not '" + std::string(keyword) + "'");
        }
    }

    static bool is_member_name(std::string_view token) {
        if (token.compare(0, 2, "::") == 0) token.remove_prefix(2);
        std::vector<std::string_view> parts;
        for (std::size_t sep; (sep = token.find("::")) != std::string_view::npos;) {
            parts.push_back(token.substr(0, sep));
            token.remove_prefix(sep + 2);
        }
        parts.push_back(token);
        if (parts.size() < 2) return false;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            if (!core::is_identifier(parts[i])) return false;
        }
        return core::is_identifier(parts.back()) || parts.back().compare(0, 8, "operator") == 0;
    }

    void member_directive(std::string_view token, std::string_view rest) {
        if (!is_member_name(token)) {
            fail("'" + std::string(token) + "' is not a member name; write Class::member or ns::Class::member");
            return;
        }
        std::string key(token);
        if (key.compare(0, 2, "::") == 0) key.erase(0, 2);

        const auto [verb, argument] = take_word(rest);
        if (verb == "returns") {
            if (const std::optional<ir::Type> type = parse_type(argument, "returns")) {
                if (first_time(key + "|returns")) out_.overrides[key].returns = type->spelling;
            }
        } else if (verb == "param") {
            const auto [position, type_text] = take_word(argument);
            // Digits only, no leading zero (so no 0 either), and short enough not to overflow.
            if (!is_all_digits(position) || position.size() > 4 || position.front() == '0') {
                fail("'param' needs a position starting at 1 and a type, e.g. " + key + " param 1 int");
                return;
            }
            if (const std::optional<ir::Type> type = parse_type(type_text, "param")) {
                const unsigned n = static_cast<unsigned>(std::stoul(std::string(position)));
                if (first_time(key + "|param " + std::string(position))) out_.overrides[key].params[n] = type->spelling;
            }
        } else if (verb == "const" || verb == "static" || verb == "virtual" || verb == "noexcept") {
            if (!argument.empty()) {
                fail("unexpected text '" + std::string(argument) + "' after '" + std::string(verb) + "'");
                return;
            }
            if (!first_time(key + "|" + std::string(verb))) return;
            core::MemberOverride& m = out_.overrides[key];
            if (verb == "const") m.is_const = true;
            else if (verb == "static") m.is_static = true;
            else if (verb == "virtual") m.is_virtual = true;
            else m.is_noexcept = true;
        } else {
            fail("after '" + std::string(token) + "' expected returns, param, const, static, virtual or noexcept");
        }
    }

    // The statement a short directive is about: the one the comment follows on
    // its own line, the next one if it stands alone, the one it sits inside if it
    // is in the middle of one (an argument list spread over several lines).
    StatementRange target_of(const Node& comment) const {
        Node node = comment;
        while (!node.parent().is_null() && !is_block(node.parent().type())) node = node.parent();
        if (node != comment) return range_of(node, file_);

        const Node prev = skip_comments_back(comment.prev_named_sibling());
        if (!prev.is_null() && end_of(prev, file_).line == comment.line()) return range_of(prev, file_);

        const Node next = skip_comments_forward(comment.next_named_sibling());
        if (!next.is_null()) return range_of(next, file_);
        return StatementRange();
    }

    const std::string&  file_;
    core::Diagnostics&  diag_;
    Annotations         out_;
    ir::SourceLoc       where_;
    std::map<std::string, ir::SourceLoc> seen_;
};

// ---- applying the short directives -----------------------------------------------------------------------

struct ApiCall {
    FactRef       ref;
    ir::SourceLoc where;
    std::string   name;
};

}  // namespace

Annotations read_annotations(const syntax::Tree& tree, const std::string& file, core::Diagnostics& diagnostics) {
    return Reader(file, diagnostics).run(tree);
}

namespace {

CallAnnotation& annotation_of(TestFileFacts& f, const FactRef& ref) {
    switch (ref.kind) {
        case FactRef::Kind::MethodCall:  return f.method_calls[ref.index].annotation;
        case FactRef::Kind::ScopedCall:  return f.scoped_calls[ref.index].annotation;
        case FactRef::Kind::FreeCall:    return f.free_calls[ref.index].annotation;
        case FactRef::Kind::FieldAccess: return f.field_accesses[ref.index].annotation;
        case FactRef::Kind::Unresolved:
        case FactRef::Kind::None:        break;
    }
    static CallAnnotation none;
    return none;
}

std::string scoped_name(const ScopedCall& c) {
    std::string out;
    for (const std::string& s : c.scope) out += s + "::";
    return out + c.name;
}

}  // namespace

void apply_call_annotations(Classified& classified, core::Diagnostics& diagnostics) {
    TestFileFacts& facts = classified.facts;
    if (facts.annotations.shorts.empty()) return;

    // A link of a chain that was resolved (`repo.find(1).name()`: name, once find
    // is known to return an Item) is a consequence of the call it hangs from, not
    // a second call to annotate -- unless it hangs from a functional cast, which
    // is no call of the API at all.
    std::set<std::size_t> derived;
    for (const Unresolved& u : facts.unresolved) {
        if (!u.resolved || u.method_call >= facts.method_calls.size()) continue;
        const bool cast_root = classified.cast_of(u.root) != nullptr;
        if (!cast_root) derived.insert(u.method_call);
    }

    // The calls of the code under test: those some target holds on to.
    std::vector<ApiCall> api;
    for (const ClassTarget& c : classified.classes) {
        for (std::size_t i : c.method_calls) {
            if (derived.count(i) != 0) continue;
            const MethodCall& m = facts.method_calls[i];
            api.push_back({{FactRef::Kind::MethodCall, i}, m.where, m.receiver.qualified() + "::" + m.method});
        }
        for (std::size_t i : c.static_calls) {
            const ScopedCall& s = facts.scoped_calls[i];
            api.push_back({{FactRef::Kind::ScopedCall, i}, s.where, scoped_name(s)});
        }
        for (std::size_t i : c.field_accesses) {
            const FieldAccess& f = facts.field_accesses[i];
            api.push_back({{FactRef::Kind::FieldAccess, i}, f.where, f.receiver.qualified() + "::" + f.field});
        }
    }
    for (const FunctionTarget& fn : classified.functions) {
        for (std::size_t i : fn.free_calls) {
            const FreeCall& c = facts.free_calls[i];
            api.push_back({{FactRef::Kind::FreeCall, i}, c.where, c.name});
        }
        for (std::size_t i : fn.scoped_calls) {
            const ScopedCall& s = facts.scoped_calls[i];
            api.push_back({{FactRef::Kind::ScopedCall, i}, s.where, scoped_name(s)});
        }
    }
    std::stable_sort(api.begin(), api.end(), [](const ApiCall& a, const ApiCall& b) {
        return std::tie(a.where.line, a.where.column) < std::tie(b.where.line, b.where.column);
    });

    for (const ShortAnnotation& s : facts.annotations.shorts) {
        if (!s.target.valid()) {
            diagnostics.error(core::code::annotation, "there is no statement after this annotation to apply it to", s.where);
            continue;
        }
        std::vector<const ApiCall*> inside;
        for (const ApiCall& call : api) {
            if (s.target.contains(call.where)) inside.push_back(&call);
        }
        if (inside.empty()) {
            diagnostics.error(core::code::annotation,
                              "the statement this annotation applies to has no call to the code under test",
                              s.where);
            continue;
        }
        if (inside.size() > 1) {
            std::string names;
            for (std::size_t i = 0; i < inside.size() && i < 4; ++i) names += (i == 0 ? "" : ", ") + inside[i]->name;
            if (inside.size() > 4) names += ", ...";
            diagnostics.error(core::code::annotation,
                              "the statement this annotation applies to has " + std::to_string(inside.size()) +
                                  " calls to the code under test (" + names +
                                  "); annotate a member instead: Class::member returns TYPE",
                              s.where);
            continue;
        }

        CallAnnotation& target = annotation_of(facts, inside.front()->ref);
        if (s.returns && target.returns) {
            diagnostics.error(core::code::annotation,
                              "'" + inside.front()->name + "' already has a 'returns' annotation (at line " +
                                  std::to_string(target.returns->where.line) + ")",
                              s.where);
            continue;
        }
        if (s.returns) target.returns = s.returns;
        target.overload = target.overload || s.overload;
        target.ignore = target.ignore || s.ignore;
    }
}

}  // namespace mt::frontend
