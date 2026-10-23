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
#include "mirrortest/frontend/facts.h"
#include "mirrortest/ir/model.h"

namespace mt::frontend {

struct ClassifyOptions {
    // Uses default_namespace, external_types, external_functions and ignore_macros.
    core::Config config;

    // The headers the test includes, already read (design 7.10). Their classes,
    // enums and functions are never generated.
    std::vector<ir::Model> known;

    // `// @mt: class Foo namespace acme` (7.11), keyed by the class's simple
    // name. It decides where an otherwise unqualified class goes.
    std::map<std::string, std::string> class_namespace;
};

// A class the tests need and nobody provides.
struct ClassTarget {
    std::vector<std::string> ns;
    std::string              name;
    std::vector<std::string> instantiations;   // `Box<int>`: "int"; distinct, in order of appearance
    ir::SourceLoc            first_use;

    // Indices into Classified::facts.
    std::vector<std::size_t> method_calls;     // obj.m(), including the pending calls found to be its own
    std::vector<std::size_t> static_calls;     // scoped_calls: Foo::m()
    std::vector<std::size_t> constructions;
    std::vector<std::size_t> field_accesses;

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
};

Classified classify(TestFileFacts facts, const ClassifyOptions& options, core::Diagnostics& diagnostics);

}  // namespace mt::frontend

#endif  // MT_FRONTEND_CLASSIFY_H
