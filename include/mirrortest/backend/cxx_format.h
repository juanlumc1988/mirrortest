// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// C++ declarations, definitions and stub bodies as text, from the IR
// (docs/design.md, sections 8.2 and 8.3).
//
// This is the "already formatted" half of the contract with the templates: a
// template places a line, it never builds one. Keeping every decision about
// how a signature is spelt here means it is covered by plain unit tests, and a
// project that overrides a template cannot change what is declared.
//
// The functions take no configuration beyond what they are given and touch no
// state; the same input is the same text on every host.

#ifndef MT_BACKEND_CXX_FORMAT_H
#define MT_BACKEND_CXX_FORMAT_H

#include <string>
#include <vector>

#include "mirrortest/core/config.h"
#include "mirrortest/ir/model.h"

namespace mt::backend {

// `type name`, written so that the name lands where C++ wants it: inside the
// parentheses of a function pointer (`void (*cb)(int)`), before the brackets
// of an array (`int values[3]`). An empty name gives the bare type.
std::string declare(const ir::Type& type, const std::string& name);

// ---- members --------------------------------------------------------------------------------------------------

// What goes inside the class: `explicit Foo(int arg1);`, `static int count();`,
// `virtual ~Foo();`, `bool operator==(const Foo& other) const;`.
std::string method_declaration(const ir::Method& method);

// What precedes the body of the definition outside the class, without the
// class's namespace (the definition sits inside it):
//   `unsigned int Foo::size() const`
//   `Foo::Foo(int /*arg1*/)`
// Parameter names are commented out, so an unused parameter is not a warning
// and the definition does not claim a name the header might later change.
// `static`, `virtual`, `explicit`, `override`, `final`, `= 0` and default
// arguments belong to the declaration only.
std::string method_definition_head(const ir::Class& cls, const ir::Method& method);

// Whether the method is defined in the .cpp: not pure, deleted or defaulted.
bool needs_definition(const ir::Method& method);

// `static int count;` / `int x;`
std::string field_declaration(const ir::Field& field);

// `int Foo::count{};` for a static field, which needs a definition; empty for
// the others.
std::string field_definition(const ir::Class& cls, const ir::Field& field);

// ---- free functions -------------------------------------------------------------------------------------------

// `int add(int arg1, int arg2);`
std::string function_declaration(const ir::Function& function);
// `int add(int /*arg1*/, int /*arg2*/)`
std::string function_definition_head(const ir::Function& function);

// ---- text -----------------------------------------------------------------------------------------------------

// The inside of a string literal: `"`, `\`, newline, carriage return and tab
// escaped, everything else as it is.
std::string escape_literal(const std::string& text);

// `namespace a {` for each level, outermost first; and `}  // namespace a` for
// each, innermost first. Empty for the global namespace.
std::vector<std::string> namespace_open(const std::vector<std::string>& ns);
std::vector<std::string> namespace_close(const std::vector<std::string>& ns);

// ---- messages -------------------------------------------------------------------------------------------------

// A signature as a person says it, for the message of a stub: qualified, the
// parameter types without names, and the qualifiers that tell overloads apart.
//   `acme::Foo::size() const`  `acme::Foo::set(int, const std::string&)`
std::string method_signature(const ir::Class& cls, const ir::Method& method);
std::string function_signature(const ir::Function& function);

// "TODO(mt): <note>" as a comment line.
std::string todo_comment(const std::string& note);

// ---- stub bodies ----------------------------------------------------------------------------------------------

struct Stub {
    std::vector<std::string> body;   // one statement per line, no braces or indentation; empty = `{}`
    std::string include;             // the system header the body needs ("stdexcept"), or ""
    // The `default` policy wanted `return {};` and the return type cannot take
    // it, so the body aborts instead.
    bool fell_back = false;
};

// The body of a stub for something that returns `ret` and is called
// `signature` in messages.
//
//   throw    `throw std::logic_error("mirrortest: not implemented: <signature>");`
//   abort    `std::abort();`
//   default  `return {};`; nothing for void; abort for a return type that
//            cannot be `{}` (`default_returnable` false, or a reference)
//
// `trivial` is for constructors and destructors: an empty body whatever the
// policy. A constructor that threw would stop every test at its first line
// with the same message, hiding which methods are missing; an empty one lets the
// test run on to the first stub that fails. A destructor must not throw at all.
Stub make_stub(core::StubPolicy policy, const ir::Type& ret, bool trivial, bool default_returnable, const std::string& signature);

}  // namespace mt::backend

#endif  // MT_BACKEND_CXX_FORMAT_H
