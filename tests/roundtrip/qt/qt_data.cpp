// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// What the header's author wrote for calc.h: the definitions the tests link against.

#include "calc.h"

namespace acme {

int Calc::add(int a, int b) const { return a + b; }

void Calc::set(const std::string&, double, const char*) {}

int Calc::twice(int x) { return 2 * x; }

int Calc::none() const { return 0; }

void Calc::take(std::string&&) {}

void Calc::own(std::unique_ptr<int>) {}

void Calc::fill(std::map<int, int>) {}

}  // namespace acme
