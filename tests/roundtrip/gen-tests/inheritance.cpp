// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The classes of hierarchy.h as a project would have implemented them. The Inherits tests are
// static_asserts: they compile against the real hierarchy and, being all there is to them, pass;
// the others must stay red.

#include "hierarchy.h"

namespace acme {

std::string Named::name() const { return std::string(); }
bool Marker::marked() const { return false; }
Leaf::Leaf() {}
int Leaf::depth() const { return 0; }
int Plain::size() const { return 0; }

}  // namespace acme
