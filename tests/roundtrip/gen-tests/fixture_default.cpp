// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

// The fixture builds a Stack before every test, so unlike the plain skeletons these
// functions do run, even though every test is red.

#include "stack.h"

Stack::Stack() {}
Stack::Stack(const Stack&) {}
Stack::~Stack() {}
void Stack::push(const std::string&) {}
bool Stack::empty() const { return true; }
Stack Stack::make() { return Stack(); }
