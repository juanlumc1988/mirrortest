// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "stack.h"

Stack::Stack() {}
Stack::Stack(const Stack&) {}
Stack::~Stack() {}
void Stack::push(const std::string&) {}
bool Stack::empty() const { return true; }
