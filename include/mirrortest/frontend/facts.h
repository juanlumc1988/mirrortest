// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// What a test file says about the code under test, before anyone decides what
// to do about it (docs/design.md, sections 7.1-7.4).
//
// The test frontend reads the tests and records FACTS: this object was
// constructed with these arguments, this method was called on it, its result
// was compared with that. It decides nothing -- not which names are classes to
// generate and which belong to std or Qt (7.7), not what type a parameter
// should have (7.9). Keeping the two halves apart is what lets each be tested
// alone: "C++ in, facts out" here, "facts in, model out" in the inference.
//
// Facts refer to each other by index into the vectors of TestFileFacts, never
// by pointer, so a TestFileFacts can be copied and moved freely.

#ifndef MT_FRONTEND_FACTS_H
#define MT_FRONTEND_FACTS_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mirrortest/frontend/literals.h"
#include "mirrortest/ir/model.h"

namespace mt::frontend {

// A class as a test names it: `ns::Foo`, or `Box<int>` (name "Box", template
// arguments "int").
struct TypeName {
    std::vector<std::string> scope;     // {"ns"} for ns::Foo
    std::string              name;
    std::string              template_args;

    std::string qualified() const;      // "ns::Foo", template arguments not included
    bool empty() const { return name.empty(); }

    friend bool operator==(const TypeName& a, const TypeName& b) {
        return a.scope == b.scope && a.name == b.name && a.template_args == b.template_args;
    }
};

// Splits "::ns::Foo<int, std::string>" into a TypeName. Qualifiers, `*`, `&`
// and `const` are not part of a name and must be stripped by the caller (use
// ir::Type::base).
TypeName parse_type_name(std::string_view spelling);

// Which fact an expression's value came from, so that whoever uses the value
// can tell the fact how.
struct FactRef {
    enum class Kind { None, MethodCall, ScopedCall, FreeCall, FieldAccess, Unresolved };
    Kind        kind = Kind::None;
    std::size_t index = 0;

    bool valid() const { return kind != Kind::None; }
};

// How the result of a call, or of a field read, is used. The first use found
// wins: a result compared inside EXPECT_EQ is Compared, not also an Argument.
enum class UseKind {
    Unknown,     // not (yet) used in a way that says anything
    Discarded,   // `foo.run();`            -> void, with a warning
    Compared,    // EXPECT_EQ(foo.size(), 3u), `n < foo.size()`
    Condition,   // EXPECT_TRUE(foo.empty()), `if (foo.empty())`
    Assigned,    // `int n = foo.count();`, `n = foo.count();`
    AutoBound,   // `auto n = foo.count();`: the type is exactly what is missing
    Argument,    // `bar(foo.size())`: bar's parameter type says nothing about it
    Receiver,    // `foo.make().run()`: the next call needs its type
};

// `hint` is the type the context implies. An AutoBound result has none when
// it is bound, but may get one from how the variable is used afterwards:
// `auto n = foo.count(); EXPECT_EQ(n, 3u);` leaves count() AutoBound with the
// hint `unsigned int`. The kind stays AutoBound -- the declaration claimed the
// result first -- so the inference can tell the type was never written.
struct ResultUse {
    UseKind                 kind = UseKind::Unknown;
    std::optional<ir::Type> hint;
};

// One argument of a call or construction.
struct ValueInfo {
    std::string text;                       // the expression as written
    ir::SourceLoc where;

    // The type it is known to have: a literal's, a variable's declared type, a
    // cast's target. Absent when nothing says.
    std::optional<ir::Type> type;
    std::vector<LiteralWarning> warnings;   // from classifying a literal; reported only if the type is used

    bool is_string_literal = false;         // an ordinary "..." whose type the context decides
    bool is_null_pointer = false;
    bool is_lvalue = false;                 // a named variable or member
    bool is_moved = false;                  // std::move(x)
    bool is_address_of = false;             // &x
    std::string var_name;                   // if the expression is just an identifier

    FactRef from;                           // if it is a call or field read of the code under test

    // If the expression is a variable that `auto` bound to such a call
    // (`auto n = foo.count();`, then `n`): that call. What the variable is later
    // compared with says something about the call's result; see ResultUse.
    FactRef auto_origin;
};

struct MethodCall {
    TypeName    receiver;
    std::string method;
    std::vector<ValueInfo> args;
    bool        on_const = false;           // called through a const object, reference or pointer
    bool        via_arrow = false;
    ResultUse   use;
    ir::SourceLoc where;                    // the method name
    std::vector<std::string> ns;            // namespaces enclosing the test
};

// `Foo::create(1.0f)` or `helper::run(2)`: whether Foo is a class or a
// namespace is not something the syntax says (design R13).
struct ScopedCall {
    std::vector<std::string> scope;
    std::string name;
    std::vector<ValueInfo> args;
    ResultUse   use;
    ir::SourceLoc where;
    std::vector<std::string> ns;
};

// An unqualified call: a function of the code under test, a macro, a libc
// function, or a functional cast `Foo(3)`. Classification (7.7) sorts them.
struct FreeCall {
    std::string name;
    std::vector<ValueInfo> args;
    ResultUse   use;
    ir::SourceLoc where;
    std::vector<std::string> ns;

    // The class of the file whose method contains the call (a fixture's helper
    // calling another of its own methods); empty in TEST bodies and free functions.
    std::string owner;
};

struct FieldAccess {
    TypeName    receiver;
    std::string field;
    bool        on_const = false;
    bool        via_arrow = false;
    ResultUse   use;                        // Assigned: `obj.x = 5;` says what x is
    ir::SourceLoc where;
    std::vector<std::string> ns;
};

struct Construction {
    enum class Form {
        Default,     // Foo foo;
        Direct,      // Foo foo(3, "a");
        Brace,       // Foo foo{3, "a"};
        Copy,        // Foo foo = 5;
        Functional,  // Foo g = Foo(3);
        New,         // new Foo(3)
        MakeUnique,  // std::make_unique<Foo>(3), also make_shared
    };
    TypeName    type;
    Form        form = Form::Default;
    std::vector<ValueInfo> args;
    ir::SourceLoc where;
    std::vector<std::string> ns;
};

// A type named in a declaration, a `new`, a cast or a template argument.
struct TypeUse {
    TypeName    type;
    bool        from_throw_assertion = false;   // EXPECT_THROW(stmt, ThisType)
    ir::SourceLoc where;
    std::vector<std::string> ns;
};

// Something the frontend could not resolve and will not guess; the inference
// applies the unknown-type policy (design D5) to each.
struct Unresolved {
    enum class Kind {
        ChainedCall,   // `foo.make().run()`: what make() returns is not known
    };
    Kind        kind = Kind::ChainedCall;
    std::string text;
    ir::SourceLoc where;

    // The call whose result is the receiver. Classification (7.7) may find that
    // it was a functional cast after all -- in `Foo(3).size()` the root is a
    // FreeCall named Foo -- and then there is nothing unresolved left.
    FactRef     root;

    // The call made on that result, waiting for a receiver (`.size()` above):
    // everything but `receiver`, which stays empty. Its `use` is where contexts
    // that consume the call's value record themselves, as for any other call.
    MethodCall  call;

    // Set by classification when the root turned out to be a class: the pending
    // call then exists as an ordinary MethodCall (`method_call` is its index)
    // and there is nothing left to resolve.
    bool        resolved = false;
    std::size_t method_call = 0;
};

struct TestCaseInfo {
    std::string kind;                       // "TEST", "TEST_F", "TEST_P"
    std::string suite;                      // the suite, or for TEST_F/TEST_P the fixture
    std::string name;
    ir::SourceLoc where;
    std::vector<std::string> ns;
};

struct TestFileFacts {
    std::string file;
    std::vector<ir::Include> includes;
    std::vector<std::string> using_namespaces;   // `using namespace ns;` anywhere in the file
    std::vector<TypeName>    using_declarations; // `using acme::Foo;` anywhere in the file
    std::vector<std::string> local_type_names;   // aliases and typedefs the file declares: never generated

    // Classes, enums and functions the test file itself defines: fixtures and
    // helpers. They are never generated, and their members give the symbol
    // table its types.
    ir::Model local;

    std::vector<TestCaseInfo> tests;
    std::vector<MethodCall>   method_calls;
    std::vector<ScopedCall>   scoped_calls;
    std::vector<FreeCall>     free_calls;
    std::vector<FieldAccess>  field_accesses;
    std::vector<Construction> constructions;
    std::vector<TypeUse>      type_uses;
    std::vector<Unresolved>   unresolved;
};

// Which test frameworks the file uses, from its includes and test macros.
struct FrameworkEvidence {
    bool gtest = false;
    bool qtest = false;
};
FrameworkEvidence detect_framework(const TestFileFacts& facts);

}  // namespace mt::frontend

#endif  // MT_FRONTEND_FACTS_H
