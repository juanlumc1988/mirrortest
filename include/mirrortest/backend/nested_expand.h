// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// Nested classes, given a place of their own (docs/design.md, section 9.1).
//
// The code frontend reads `struct Leaf` inside `class Tree` as one of Tree's nested classes, and
// a test is written in the namespace of the class it is for, where the only name that reaches
// Leaf is `Tree::Leaf`. So every public class nested in a class of the model (and in the public
// ones nested in those) becomes a class of the model, right after the class that holds it:
//
//   class Tree { public: struct Node { Node(int key); }; class Leaf { public: Node first() const; }; };
//
//   -> class Tree { ... };  class Tree::Node { Node(int key); };  class Tree::Leaf { Tree::Node first() const; };
//
// The new class is called `Tree::Leaf` and has the path to it in `enclosing`. The types its
// members mention are written as in the class (`Node`, which is `Tree::Node` out of it), so they
// are qualified here with the nested types of every class around it; the ones of its own are
// qualified by the planner like those of any class.

#ifndef MT_BACKEND_NESTED_EXPAND_H
#define MT_BACKEND_NESTED_EXPAND_H

#include "mirrortest/ir/model.h"

namespace mt::backend {

// `model` with its public nested classes made classes of their own. A protected or private one, a
// class template (it needs instantiations of its own) and what is nested in either are left out:
// a test cannot name the first, and has no arguments for the second.
ir::Model flatten_nested_classes(const ir::Model& model);

}  // namespace mt::backend

#endif  // MT_BACKEND_NESTED_EXPAND_H
