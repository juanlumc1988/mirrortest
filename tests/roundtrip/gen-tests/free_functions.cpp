// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// An implementation of math.h: what the tests of its free functions are linked with.

#include "math.h"

namespace calc {

int add(int a, int b) { return a + b; }
double add(double a, double b) { return a + b; }
long long scale(long long value, int factor) { return value * factor; }
std::string label(unsigned id) noexcept { return std::to_string(id); }
bool operator<(const std::string& name, int rank) { return static_cast<int>(name.size()) < rank; }

namespace inner {
void ping() {}
}  // namespace inner

}  // namespace calc

int counter() { return 0; }

extern "C" {
int c_version(void) { return 1; }
}
