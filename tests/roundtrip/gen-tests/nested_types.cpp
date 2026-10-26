// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "tree.h"

Tree::Node Tree::root() const {
    Node node = {0};
    return node;
}
void Tree::walk(Order, const Node&) {}
