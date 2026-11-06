// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// A function template has no definition to link: tools.h is all of it. This translation unit checks that
// the header stands alone as C++11 and instantiates what the configuration lists, as the tests will; the one
// function that is not a template needs its definition.

#include "tools.h"

namespace acme {

template int clamp_to<int>(int, int, int);
template double clamp_to<double>(double, double, double);
template int scaled<int, 4>(int) noexcept;
template long scaled<long, 8>(long) noexcept;
template int lookup<std::string, int>(const std::string&, int);
template void reset<int>(int&);

int plain(int v) { return v; }

}  // namespace acme
