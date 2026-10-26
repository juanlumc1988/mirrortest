// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The class the generated tests are for, as a project would have implemented it
// (docs/design.md, section 16.3): just enough for the tests to link. They must
// still fail: they say so with FAIL() before they call anything.

#include "counter.h"

Counter::Counter(int) {}
int Counter::value() const { return 0; }
void Counter::increment() {}
void Counter::reset() {}
