// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "setter.h"

void Setter::set(int) {}
void Setter::set(double) {}
void Setter::set(const std::string&) {}
int Setter::size() { return 0; }
int Setter::size() const { return 0; }
