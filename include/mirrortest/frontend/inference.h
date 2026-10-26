// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Inference: what the tests say, turned into the classes and functions that
// satisfy them (docs/design.md, section 7.9).
//
// It takes the classified facts and decides, for every class and function to
// generate, which constructors, methods, parameters, return types, `const`,
// `static` and fields the uses add up to. Where several uses disagree the one
// with the higher Origin wins and `mt-conflict` says so; where nothing says,
// the policy of D5 applies -- an error, or (with `auto_policy: todo`) a
// warning and a placeholder type -- and a guess is never passed off as
// something stated: every type that was inferred carries a note saying from
// what.
//
// A pure function: no I/O, and the same input always gives the same model.

#ifndef MT_FRONTEND_INFERENCE_H
#define MT_FRONTEND_INFERENCE_H

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

#include "mirrortest/core/config.h"
#include "mirrortest/core/diagnostics.h"
#include "mirrortest/frontend/classify.h"
#include "mirrortest/ir/model.h"

namespace mt::frontend {

// The type each call of the code under test was inferred to return, by fact:
// what lets a later pass see that `foo.make().run()` calls run on a Bar.
// Only results that something actually said are here; a placeholder under
// `auto_policy: todo` is a guess and is left out.
class ResultTypes {
public:
    const ir::Type* find(const FactRef& ref) const;
    void set(const FactRef& ref, const ir::Type& type);
    std::size_t size() const { return types_.size(); }

private:
    std::map<std::pair<int, std::size_t>, ir::Type> types_;
};

// `known` are the headers the test includes, already read (design 7.10): here
// they only tell which enums exist, since an enum is passed by value.
//
// An argument that is the result of another call of the code under test takes
// that call's type (or, for a functional cast, the class it constructs):
// `bar.set(foo.size())` makes set's parameter whatever size() returns.
// Results are filled in if `results` is given.
ir::Model infer(const Classified& classified, const core::Config& config, const std::vector<ir::Model>& known,
                core::Diagnostics& diagnostics, ResultTypes* results = nullptr);

}  // namespace mt::frontend

#endif  // MT_FRONTEND_INFERENCE_H
