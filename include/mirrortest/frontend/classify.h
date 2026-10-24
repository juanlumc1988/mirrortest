// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Name classification: facts in, "what to generate" out (docs/design.md,
// sections 7.7 and 7.8).
//
// The test frontend reports what the test says; this decides which of the
// names it mentions are code the user must write. A name is generated when
// the test uses it as a class (a declared type, a `new`, a receiver) or a
// function (an unqualified call) and nothing else already provides it: not
// the standard library, Qt or GoogleTest, not the test file itself, not a
// header it includes, not the configuration's external lists.
//
// The syntax leaves three questions open, and this is where they are closed:
//
//   * Foo::create(): is Foo a class (a static method) or a namespace
//     (a function)? (R13)
//   * Foo(3): a functional cast of a class, or a call to a function named Foo?
//   * foo.make().run(), `auto x = Foo(3); x.run();`: once Foo is known to be a
//     class, the receiver is known.
//
// The result refers to the facts by index and does not copy them, except for
// the few facts classification itself adds (a functional cast found to be a
// construction, a pending call given its receiver).

#ifndef MT_FRONTEND_CLASSIFY_H
#define MT_FRONTEND_CLASSIFY_H

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "mirrortest/core/config.h"
#include "mirrortest/core/diagnostics.h"
#include "mirrortest/frontend/annotations.h"
#include "mirrortest/frontend/facts.h"
#include "mirrortest/ir/model.h"

namespace mt::frontend {

struct ClassifyOptions {
    // Uses default_namespace, external_types, external_functions and ignore_macros.
    core::Config config;

    // The headers the test includes, already read (design 7.10). Their classes,
    // enums and functions are never generated.
    std::vector<ir::Model> known;
};

// A class the tests need and nobody provides.
struct ClassTarget {
    std::vector<std::string> ns;
    std::string              name;
    std::vector<std::string> instantiations;   // `Box<int>`: "int"; distinct, in order of appearance
    ir::SourceLoc            first_use;
    std::string              header;           // `// @mt: class Foo header "acme/foo.h"`, or empty

    // Indices into Classified::facts.
    std::vector<std::size_t> method_calls;     // obj.m(), including the pending calls found to be its own
    std::vector<std::size_t> static_calls;     // scoped_calls: Foo::m()
    std::vector<std::size_t> constructions;
    std::vector<std::size_t> field_accesses;

    // What the test's doubles of this class override: a fake or a mock that derives
    // from it (`class MockSensor : public Sensor`) says which functions it is an
    // interface of. Each as the double declares it, made virtual (inference makes it
    // pure unless the test builds the class itself); one per signature, whichever
    // double said it first.
    std::vector<ir::Method>  interface_methods;
    std::vector<std::string> doubles;          // the names of those doubles, in the order the file defines them

    std::string qualified() const;             // "acme::Foo"
};

// A free function the tests need and nobody provides.
struct FunctionTarget {
    std::vector<std::string> ns;
    std::string              name;
    ir::SourceLoc            first_use;

    std::vector<std::size_t> free_calls;       // into facts.free_calls: f(...)
    std::vector<std::size_t> scoped_calls;     // into facts.scoped_calls: ns::f(...)

    std::string qualified() const;
};

struct Classified {
    // The facts as read, plus what classification found out: constructions
    // added for functional casts, method calls for pending calls whose receiver
    // turned out to be a class. `Unresolved::resolved` marks the latter.
    TestFileFacts facts;

    // Both in order of first use in the source.
    std::vector<ClassTarget>    classes;
    std::vector<FunctionTarget> functions;

    // The type each functional cast constructs, by index into facts.free_calls:
    // a class to generate (qualified, as it will be declared) or somebody else's
    // type (as written). A cast is a call whose result has a type the syntax did
    // not give, so `process(Foo(3))` can type process's parameter.
    std::map<std::size_t, TypeName> casts;
    // The same for `ns::Foo(3)`, by index into facts.scoped_calls.
    std::map<std::size_t, TypeName> scoped_casts;

    // The type a call constructs if it is a functional cast, else null.
    const TypeName* cast_of(const FactRef& call) const;

    // The class each of the file's test doubles stands for, by the double's name, as the
    // test writes it: "MockSensor" -> "Sensor". A double is a class of the file with a
    // base that is neither one of the file's own classes nor part of the test framework.
    // The code under test is written against the interface and the test hands it a
    // double, so a parameter that receives one has the interface's type.
    std::map<std::string, std::string> doubles;

    // config.overrides with the file's long-form annotations laid over them,
    // attribute by attribute: the annotation, being nearer the code, wins. Keyed
    // "Class::member" or "ns::Class::member" as written.
    std::map<std::string, core::MemberOverride> overrides;
};

Classified classify(TestFileFacts facts, const ClassifyOptions& options, core::Diagnostics& diagnostics);

}  // namespace mt::frontend

#endif  // MT_FRONTEND_CLASSIFY_H
