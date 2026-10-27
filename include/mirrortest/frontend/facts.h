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

#include "mirrortest/frontend/annotations.h"
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

// True for "int", "unsigned long", "void": a spelling made only of the words of
// the fundamental types.
bool is_fundamental_name(std::string_view spelling);

// std::unique_ptr, std::shared_ptr, QScopedPointer, QSharedPointer, QPointer.
bool is_smart_pointer(const TypeName& name);

// "int, std::map<int, int>" -> "int".
std::string first_template_argument(const std::string& args);

// The class an ir::Type names, or nothing for fundamentals, `auto` and the
// exotic (function pointers, arrays). Pointers and references are looked
// through: `const Foo&` names Foo.
std::optional<TypeName> class_name_of(const ir::Type& type);

// What `.` or `->` on a value of this type reaches: `Foo` and `Foo&` through a
// dot, `Foo*` through an arrow, `unique_ptr<Foo>` through an arrow (and
// unique_ptr itself through a dot). Nothing for the combinations C++ does not allow.
struct Receiver {
    TypeName cls;
    bool     on_const = false;
};
std::optional<Receiver> receiver_of_type(const ir::Type& type, bool arrow);

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

    // The warnings of the literal the hint was taken from (`EXPECT_EQ(f(),
    // 5000000000)`): reported only if the hint ends up being used.
    std::vector<LiteralWarning> hint_warnings;

    // The hint is the type of `Color::Red` (an index into TestFileFacts::enumerators), which is a
    // type only if Color is an enum: classification (7.7) says so, and then sets `hint`.
    std::optional<std::size_t> hint_enumerator;

    // The result was given to a matcher that says nothing of its type (`EXPECT_THAT(f(), ElementsAre(1, 2))`): its name,
    // for the note that says why the type is not known.
    std::string matcher;
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

    // If the expression is `Color::Red` (an index into TestFileFacts::enumerators): it has the type
    // of an enum only if Color is one, which the syntax does not say; classification (7.7) does,
    // and then gives `type` to the value.
    std::optional<std::size_t> enumerator;

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
    bool        writes_result = false;      // the result is assigned to: `a[i] = 3`, so the call is an lvalue
    ResultUse   use;
    CallAnnotation annotation;              // `// @mt:` on the statement (7.11)
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
    CallAnnotation annotation;
    ir::SourceLoc where;
    std::vector<std::string> ns;
};

// An unqualified call: a function of the code under test, a macro, a libc
// function, or a functional cast `Foo(3)`. Classification (7.7) sorts them.
struct FreeCall {
    std::string name;
    std::vector<ValueInfo> args;
    ResultUse   use;
    CallAnnotation annotation;
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
    CallAnnotation annotation;
    ir::SourceLoc where;
    std::vector<std::string> ns;
};

// `Color::Red` as a value, neither called nor a type: an enumerator of `Color` if that is an enum.
// It is as likely a static member of a class (`Limits::kMax`) or a constant of a namespace
// (`acme::kVersion`); classification (7.7) tells them apart.
struct Enumerator {
    TypeName      scope;   // `Color`, `acme::Color`
    std::string   name;    // `Red`
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
    std::string kind;                       // "TEST", "TEST_F", "TEST_P"; "SLOT" for a slot of a Qt Test class
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
    bool                      qtest_main = false;   // QTEST_MAIN(...) and its variants
    std::vector<MethodCall>   method_calls;
    std::vector<ScopedCall>   scoped_calls;
    std::vector<FreeCall>     free_calls;
    std::vector<FieldAccess>  field_accesses;
    std::vector<Construction> constructions;
    std::vector<TypeUse>      type_uses;
    std::vector<Unresolved>   unresolved;
    std::vector<Enumerator>   enumerators;

    Annotations               annotations;       // every `// @mt:` comment of the file (7.11)
};

// Which test frameworks the file uses, from its includes and test macros.
struct FrameworkEvidence {
    bool gtest = false;
    bool qtest = false;
};
FrameworkEvidence detect_framework(const TestFileFacts& facts);

}  // namespace mt::frontend

#endif  // MT_FRONTEND_FACTS_H
