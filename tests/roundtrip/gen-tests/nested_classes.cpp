// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The classes of tree.h as a project would have implemented them, nested ones included: the
// tests reach them as Tree::Node and Tree::Cursor and must still fail (Inherits aside).

#include "tree.h"

namespace acme {

Tree::Node::Node() {}
Tree::Node::Node(int) {}
int Tree::Node::key() const { return 0; }
Tree::Node Tree::Cursor::current() const { return Node(); }
void Tree::Cursor::advance(const Step&) {}
int Tree::size() const { return 0; }
void Tree::add(const Node&) {}
void Tree::Hidden::touch() {}

}  // namespace acme
