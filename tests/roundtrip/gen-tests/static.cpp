// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "util.h"

int Util::clamp(int value, int, int) { return value; }
Util Util::make() { return Util(); }
void Util::log(const char*) {}
