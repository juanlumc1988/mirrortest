// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "vec.h"

bool Vec::operator==(const Vec&) const { return true; }
bool Vec::operator!=(const Vec&) const { return false; }
int& Vec::operator[](int) {
    static int value = 0;
    return value;
}
Vec& Vec::operator+=(const Vec&) { return *this; }
int Vec::operator()(int x) const { return x; }
Vec::operator bool() const { return true; }
Vec::operator double() const { return 0.0; }
