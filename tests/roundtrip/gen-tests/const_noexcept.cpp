// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "size.h"

unsigned Buffer::size() const noexcept { return 0; }
bool Buffer::full() const { return false; }
void Buffer::clear() noexcept {}
int& Buffer::at(unsigned) {
    static int value = 0;
    return value;
}
const int& Buffer::at(unsigned) const {
    static const int value = 0;
    return value;
}
