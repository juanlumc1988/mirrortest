// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Class templates, instantiated (docs/design.md, section 9.1).
//
// A test cannot call `Box<T>::get()`; it calls `Box<int>::get()`. Which T the
// tests are for is the configuration's to say (`template_instantiations`), so
// this turns every class template it names into one class per instantiation,
// with the template parameters replaced in every type the class mentions:
//
//   template <typename T, int N = 4> class Box { public: T get(int i) const; Box clone() const; };
//   "acme::Box": ["int", "std::string, 8"]
//
//   -> class Box<int>          { public: int get(int i) const; Box<int> clone() const; };
//      class Box<std::string, 8> { public: std::string get(int i) const; Box<std::string, 8> clone() const; };
//
// The new class is called `Box<int>`: the planner writes that wherever it names
// the class, and a name with `<` in it is what tells a plan it is an instantiation.

#ifndef MT_BACKEND_TEMPLATE_EXPAND_H
#define MT_BACKEND_TEMPLATE_EXPAND_H

#include <string>
#include <vector>

#include "mirrortest/core/config.h"
#include "mirrortest/core/diagnostics.h"
#include "mirrortest/ir/model.h"

namespace mt::backend {

// One template parameter, as `template <...>` declares it.
struct TemplateParam {
    std::string name;            // "T"; empty if the parameter is unnamed
    std::string default_value;   // "" if none
    bool        has_default = false;
};

// "typename T, int N = 4, typename A = std::allocator<T>" -> three parameters. False if a
// parameter is a pack (`typename... Ts`), which has no single argument to put in its place.
bool parse_template_params(const std::string& text, std::vector<TemplateParam>& out);

// "int, std::map<int, int>, 8" -> {"int", "std::map<int, int>", "8"}: the commas outside <>, () and [].
std::vector<std::string> split_template_arguments(const std::string& text);

// `model` with each class template that `config.template_instantiations` names (by its qualified
// name) replaced, where it stood, by its instantiations in the order the configuration lists them.
// Other classes, and templates the configuration does not name, are as they were. Reports through
// `diagnostics`, as mt-config warnings, an instantiation it cannot make: too many arguments, too
// few for parameters without a default, a parameter pack.
ir::Model instantiate_templates(const ir::Model& model, const core::Config& config, core::Diagnostics& diagnostics);

}  // namespace mt::backend

#endif  // MT_BACKEND_TEMPLATE_EXPAND_H
