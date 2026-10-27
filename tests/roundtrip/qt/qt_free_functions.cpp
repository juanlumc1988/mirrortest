// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "words.h"

namespace text {

std::string trim(const std::string& value) { return value; }
std::string join(const std::string& a, const std::string& b) { return a + b; }
std::string join(const std::string& a, const std::string& b, const std::string& c) { return a + b + c; }
unsigned count(const std::string& value, char) noexcept { return static_cast<unsigned>(value.size()); }
void reset() {}

}  // namespace text
