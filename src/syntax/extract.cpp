// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/syntax/extract.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "mirrortest/ir/type.h"

namespace mt::syntax {

namespace {

// Nodes that wrap another declarator without being the name.
bool is_wrapper(std::string_view type) {
    return type == "pointer_declarator" || type == "reference_declarator" ||
           type == "array_declarator" || type == "parenthesized_declarator" ||
           type == "attributed_declarator" || type == "variadic_declarator" ||
           type == "abstract_pointer_declarator" || type == "abstract_reference_declarator" ||
           type == "abstract_array_declarator" || type == "abstract_parenthesized_declarator" ||
           type == "abstract_function_declarator" || type == "function_declarator" ||
           type == "init_declarator";
}

// The declarator inside a wrapper. Most wrappers store it under the field
// "declarator"; reference_declarator and the abstract ones do not label it, so
// fall back to the last child that is not a qualifier or a comment.
Node inner_declarator(const Node& wrapper) {
    Node labelled = wrapper.child_by_field("declarator");
    if (!labelled.is_null()) {
        return labelled;
    }
    for (unsigned i = wrapper.named_child_count(); i-- > 0;) {
        const Node child = wrapper.named_child(i);
        const std::string_view t = child.type();
        if (t != "type_qualifier" && t != "comment" && t != "attribute_declaration" &&
            t != "ms_pointer_modifier" && t != "parameter_list" && t != "trailing_return_type") {
            return child;
        }
    }
    return {};
}

// The text of `node` with the byte ranges of `removed` taken out.
std::string text_without(const Node& node, std::vector<Node> removed) {
    std::string text(node.text());
    const unsigned base = node.start_byte();
    std::sort(removed.begin(), removed.end(),
              [](const Node& a, const Node& b) { return a.start_byte() > b.start_byte(); });
    for (const Node& r : removed) {
        if (r.is_null() || r.start_byte() < base || r.end_byte() > node.end_byte()) {
            continue;
        }
        text.erase(r.start_byte() - base, r.end_byte() - r.start_byte());
    }
    return text;
}

// A suffix made only of stars, ampersands and cv-qualifiers: the kind the
// structured fields of ir::Type can describe.
bool suffix_is_simple(std::string_view suffix) {
    std::size_t i = 0;
    while (i < suffix.size()) {
        const char c = suffix[i];
        if (c == '*' || c == '&' || c == ' ') {
            ++i;
        } else if (suffix.compare(i, 5, "const") == 0) {
            i += 5;
        } else if (suffix.compare(i, 8, "volatile") == 0) {
            i += 8;
        } else {
            return false;
        }
    }
    return true;
}

bool ends_with(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() && s.substr(s.size() - tail.size()) == tail;
}

}  // namespace

ir::SourceLoc location(const Node& node, std::string_view file) {
    ir::SourceLoc loc;
    loc.file = std::string(file);
    loc.line = node.line();
    loc.column = node.column();
    return loc;
}

void walk(const Node& root, const std::function<bool(const Node&)>& visit) {
    if (root.is_null() || !visit(root)) {
        return;
    }
    for (unsigned i = 0; i < root.named_child_count(); ++i) {
        walk(root.named_child(i), visit);
    }
}

std::vector<Node> find_all(const Node& root, std::string_view type) {
    std::vector<Node> found;
    walk(root, [&](const Node& n) {
        if (n.type() == type) {
            found.push_back(n);
        }
        return true;
    });
    return found;
}

Node find_first(const Node& root, std::string_view type) {
    Node found;
    walk(root, [&](const Node& n) {
        if (!found.is_null()) {
            return false;
        }
        if (n.type() == type) {
            found = n;
            return false;
        }
        return true;
    });
    return found;
}

bool has_token(const Node& node, std::string_view token) {
    for (unsigned i = 0; i < node.child_count(); ++i) {
        const Node child = node.child(i);
        if (!child.is_named() && child.type() == token) {
            return true;
        }
    }
    return false;
}

std::vector<Node> children_by_field(const Node& node, std::string_view field) {
    std::vector<Node> out;
    for (unsigned i = 0; i < node.child_count(); ++i) {
        if (node.field_name_for_child(i) == field) {
            out.push_back(node.child(i));
        }
    }
    return out;
}

Declarator parse_declarator(const Node& declarator) {
    Declarator out;
    if (declarator.is_null()) {
        return out;
    }

    // "= value" of an init_declarator is not part of the type.
    Node top = declarator;
    if (top.type() == "init_declarator") {
        const Node inner = top.child_by_field("declarator");
        if (!inner.is_null()) {
            top = inner;
        }
    }

    Node current = top;
    for (int depth = 0; depth < 64 && !current.is_null(); ++depth) {   // bound: pathological trees
        const std::string_view type = current.type();
        if (!is_wrapper(type)) {
            out.name_node = current;
            break;
        }

        Node next = inner_declarator(current);
        if (type == "function_declarator" || type == "abstract_function_declarator") {
            if (!next.is_null() && next.type() == "parenthesized_declarator") {
                out.is_function_pointer = true;      // int (*cb)(int): a variable
            } else if (out.function.is_null()) {
                out.function = current;
            }
        }
        current = next;
    }

    if (!out.name_node.is_null()) {
        out.name = std::string(out.name_node.text());
    }
    std::vector<Node> removed;
    removed.push_back(out.function.is_null() ? out.name_node : out.function);
    out.suffix = ir::normalize_spelling(text_without(top, removed));
    return out;
}

Specifiers read_specifiers(const Node& declaration) {
    Specifiers s;
    s.type = declaration.child_by_field("type");

    std::string text;
    const auto append = [&text](std::string_view part) {
        if (!text.empty()) {
            text += ' ';
        }
        text += part;
    };

    for (unsigned i = 0; i < declaration.child_count(); ++i) {
        const Node child = declaration.child(i);
        const std::string_view type = child.type();
        if (!s.type.is_null() && child == s.type) {
            append(child.text());
        } else if (type == "type_qualifier") {
            const std::string_view q = child.text();
            if (q == "const") {
                s.is_const = true;
                append(q);
            } else if (q == "volatile") {
                s.is_volatile = true;
                append(q);
            } else if (q == "constexpr") {
                s.is_constexpr = true;
            } else if (q == "mutable") {
                s.is_mutable = true;
            }
        } else if (type == "storage_class_specifier") {
            const std::string_view q = child.text();
            s.is_static = s.is_static || q == "static";
            s.is_inline = s.is_inline || q == "inline";
            s.is_extern = s.is_extern || q == "extern";
        } else if (type == "explicit_function_specifier") {
            s.is_explicit = true;
        } else if (!child.is_named() && type == "virtual") {
            s.is_virtual = true;
        }
    }
    s.type_text = ir::normalize_spelling(text);
    return s;
}

ir::Type make_declared_type(const Specifiers& specifiers, const Declarator& declarator,
                            std::string_view file) {
    ir::Type t;
    if (specifiers.type_text.empty()) {
        return t;   // no type: a constructor, a destructor, a conversion
    }

    t.spelling = ir::normalize_spelling(specifiers.type_text + " " + declarator.suffix);
    t.origin = ir::Origin::Declared;
    t.where = location(specifiers.type, file);

    if (suffix_is_simple(declarator.suffix)) {
        t.base = ir::normalize_spelling(specifiers.type.text());
        t.is_const = specifiers.is_const;
        t.pointer_depth = static_cast<unsigned>(
            std::count(declarator.suffix.begin(), declarator.suffix.end(), '*'));
        if (ends_with(declarator.suffix, "&&")) {
            t.ref = ir::RefKind::RValue;
        } else if (ends_with(declarator.suffix, "&")) {
            t.ref = ir::RefKind::LValue;
        }
    } else {
        // A function pointer, an array, a pointer to member: the spelling is
        // exact, and nothing finer is claimed about it.
        t.base = t.spelling;
    }
    return t;
}

FunctionTraits read_function_traits(const Node& function_declarator, std::string_view file) {
    FunctionTraits traits;
    for (unsigned i = 0; i < function_declarator.child_count(); ++i) {
        const Node child = function_declarator.child(i);
        const std::string_view type = child.type();
        if (type == "type_qualifier") {
            traits.is_const = traits.is_const || child.text() == "const";
            traits.is_volatile = traits.is_volatile || child.text() == "volatile";
        } else if (type == "noexcept") {
            std::string compact;
            for (char c : child.text()) {
                if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                    compact += c;
                }
            }
            traits.is_noexcept = compact == "noexcept" || compact == "noexcept(true)";
        } else if (type == "virtual_specifier") {
            traits.is_override = traits.is_override || child.text() == "override";
            traits.is_final = traits.is_final || child.text() == "final";
        } else if (type == "ref_qualifier") {
            traits.ref_qualifier = child.text() == "&&" ? ir::RefKind::RValue : ir::RefKind::LValue;
        } else if (type == "trailing_return_type") {
            // "-> T": its type_descriptor is built like a parameter's type.
            const Node descriptor = find_first(child, "type_descriptor");
            if (!descriptor.is_null()) {
                traits.has_trailing_return = true;
                traits.trailing_return = make_declared_type(
                    read_specifiers(descriptor), parse_declarator(descriptor.child_by_field("declarator")),
                    file);
            }
        }
    }
    return traits;
}

std::vector<ir::Param> read_parameters(const Node& parameter_list, std::string_view file) {
    std::vector<ir::Param> params;
    for (const Node& p : parameter_list.named_children()) {
        const std::string_view type = p.type();
        if (type == "parameter_declaration" || type == "optional_parameter_declaration" ||
            type == "variadic_parameter_declaration") {
            const Specifiers specifiers = read_specifiers(p);
            const Declarator declarator = parse_declarator(p.child_by_field("declarator"));
            ir::Param param;
            param.type = make_declared_type(specifiers, declarator, file);
            param.name = declarator.name;
            const Node value = p.child_by_field("default_value");
            if (!value.is_null()) {
                // The node of a braced default (`= {}`) starts at the blank after the `=`.
                std::string_view text = value.text();
                while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
                while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
                param.default_value = std::string(text);
            }
            params.push_back(std::move(param));
        }
    }

    // C-style `(int a, ...)`: the ellipsis is an anonymous token of the list,
    // not a node, so it is easy to lose -- and f(int, ...) is not f(int).
    for (unsigned i = 0; i < parameter_list.child_count(); ++i) {
        const Node child = parameter_list.child(i);
        if (!child.is_named() && child.type() == "...") {
            ir::Param param;
            param.type.spelling = "...";
            param.type.base = "...";
            param.type.origin = ir::Origin::Declared;
            param.type.where = location(child, file);
            params.push_back(std::move(param));
        }
    }
    if (params.size() == 1 && params[0].type.spelling == "void" && params[0].name.empty()) {
        params.clear();   // f(void)
    }
    return params;
}

std::optional<QFetch> parse_qfetch(std::string_view text) {
    const auto trim = [](std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' || s.front() == '\r')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r' || s.back() == ';')) s.remove_suffix(1);
        return s;
    };
    text = trim(text);
    std::string_view rest;
    if (text.substr(0, 6) == "QFETCH" && text.size() > 6) {
        rest = trim(text.substr(6));
    } else {
        return std::nullopt;
    }
    if (rest.substr(0, 7) == "_GLOBAL") rest = trim(rest.substr(7));
    if (rest.size() < 2 || rest.front() != '(' || rest.back() != ')') return std::nullopt;
    rest = rest.substr(1, rest.size() - 2);

    int depth = 0;
    std::size_t comma = std::string_view::npos;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        const char c = rest[i];
        if (c == '(' || c == '[' || c == '{' || c == '<') ++depth;
        else if (c == ')' || c == ']' || c == '}' || c == '>') --depth;
        else if (c == ',' && depth == 0) comma = i;
        // `QFETCH(int, a), QFETCH(int, b)` is two macros, not one with odd arguments.
        if (depth < 0) return std::nullopt;
    }
    if (depth != 0 || comma == std::string_view::npos) return std::nullopt;

    QFetch out;
    out.type = std::string(trim(rest.substr(0, comma)));
    out.name = std::string(trim(rest.substr(comma + 1)));
    if (out.type.empty() || out.name.empty()) return std::nullopt;
    return out;
}

}  // namespace mt::syntax
