// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Reading declarations out of a tree-sitter tree: what the type of a
// parameter, a field or a return value is, what a function's qualifiers are.
// Shared by the frontend that reads headers and the one that reads tests.
//
// The approach, and why. C++ declarators are inside-out: in `const char* const
// names[3]` the name sits in the middle of its own type. Rather than rebuild a
// type from the shape of the declarator tree -- pointer_declarator,
// array_declarator, parenthesized_declarator, abstract_reference_declarator,
// and a dozen more, each with its own fields -- a type is read as TEXT:
//
//     type   =  the qualifiers and type specifier, in source order
//     suffix =  the declarator's own text with the name taken out
//
// so `const char* const names[3]` is "const char" + "* const [3]". That is
// right for every shape, including the ones nobody thought of, and it keeps
// exactly what the user wrote. The structured fields of ir::Type
// (is_const, ref, pointer_depth) are filled only when the suffix is the simple
// kind -- stars, ampersands, const -- and left empty for anything else
// (function pointers, arrays, pointers to members), whose `spelling` is still
// exact.
//
// The node shapes this relies on are pinned in tests/unit/test_syntax_tree.cpp
// and tests/unit/test_extract.cpp; a grammar bump that changes them fails
// there first.

#ifndef MT_SYNTAX_EXTRACT_H
#define MT_SYNTAX_EXTRACT_H

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mirrortest/ir/model.h"
#include "mirrortest/syntax/tree.h"

namespace mt::syntax {

// ---- finding nodes ---------------------------------------------------------------------

// `file` is the path as the user gave it; the position is the node's start.
ir::SourceLoc location(const Node& node, std::string_view file);

// Pre-order walk of the named nodes under `root` (root included). `visit`
// returns false to skip that node's children.
void walk(const Node& root, const std::function<bool(const Node&)>& visit);

// All named descendants of exactly this type, in source order, root included.
std::vector<Node> find_all(const Node& root, std::string_view type);

// The first of them, or a null Node.
Node find_first(const Node& root, std::string_view type);

// True if `node` has an anonymous child with exactly this text -- a keyword
// such as "virtual" or "friend", which the grammar does not give a named node.
bool has_token(const Node& node, std::string_view token);

// Every child stored under the grammar field `field`, in order. A declaration
// like `int a, *b;` has two "declarator" children.
std::vector<Node> children_by_field(const Node& node, std::string_view field);

// ---- declarators -----------------------------------------------------------------------

struct Declarator {
    Node        name_node;              // null for an abstract declarator (an unnamed parameter)
    std::string name;                   // its text; "Foo::bar" in an out-of-class definition
    Node        function;               // the function_declarator if this declares a function, else null
    bool        is_function_pointer = false;   // int (*cb)(int): looks like a function, is a variable

    // The declarator's own contribution to the type, name removed and the
    // function part too, whitespace-normalised:
    //   "const std::string& s" -> "&"          "Widget* create(int)" -> "*"
    //   "names[3]" -> "[3]"                     "(*cb)(int)" -> "(*)(int)"
    std::string suffix;
};

// `declarator` is a `declarator` field (or an abstract declarator, or an
// init_declarator, whose "= value" is not part of the type). A null node gives
// an empty Declarator: a parameter with only a type.
Declarator parse_declarator(const Node& declarator);

// ---- specifiers ------------------------------------------------------------------------

struct Specifiers {
    Node        type;                // the "type" field; null for constructors and conversions
    std::string type_text;           // const/volatile and the type, in source order: "int const"
    bool is_const = false;
    bool is_volatile = false;

    bool is_static = false;
    bool is_virtual = false;
    bool is_explicit = false;
    bool is_inline = false;
    bool is_constexpr = false;
    bool is_mutable = false;
    bool is_extern = false;
};

// Reads the specifiers of a declaration-like node: parameter_declaration,
// field_declaration, declaration, function_definition, ...
Specifiers read_specifiers(const Node& declaration);

// The type a declaration gives its declarator, as written:
//   const std::string& s  ->  "const std::string&"
//   Widget* create(int)   ->  "Widget*"            (the return type)
// Origin Declared. `auto` comes back as base "auto"; deciding what to do about
// it is the caller's.
ir::Type make_declared_type(const Specifiers& specifiers, const Declarator& declarator,
                            std::string_view file);

// ---- functions -------------------------------------------------------------------------

struct FunctionTraits {
    bool is_const = false;
    bool is_volatile = false;
    bool is_noexcept = false;        // noexcept or noexcept(true); noexcept(expr) is not
    bool is_override = false;
    bool is_final = false;
    ir::RefKind ref_qualifier = ir::RefKind::None;
    bool has_trailing_return = false;
    ir::Type trailing_return;        // valid if has_trailing_return
};

FunctionTraits read_function_traits(const Node& function_declarator, std::string_view file);

// The parameters of a parameter_list. `(void)` is no parameters; a trailing
// `...` is a parameter of type "..." with no name. Comments are skipped.
std::vector<ir::Param> read_parameters(const Node& parameter_list, std::string_view file);

// ---- QTest ---------------------------------------------------------------------------------

struct QFetch {
    std::string type;   // "std::map<int, int>"
    std::string name;   // "items"
};

// `QFETCH(T, name)` and `QFETCH_GLOBAL(T, name)`, split at the last comma that
// is outside any bracket: T may hold commas of its own (`std::map<int, int>`)
// and the name never does. The grammar cannot be asked, because what it makes of
// the macro depends on T -- a call when T is a name, an ERROR node when T is a
// builtin type (`QFETCH(int, v)` is a function declarator to it, and
// `QFETCH(unsigned long long, v)` the same). `text` may carry a trailing `;`.
// Nothing for text that is not a QFETCH.
std::optional<QFetch> parse_qfetch(std::string_view text);

}  // namespace mt::syntax

#endif  // MT_SYNTAX_EXTRACT_H
