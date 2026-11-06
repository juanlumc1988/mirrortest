// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The header is all of a function template; this checks that it stands alone and instantiates what the tests use.

#include "pairs.h"

namespace acme {

template int larger<int>(int, int);
template double larger<double>(double, double);
template long times<long, 2>(long) noexcept;

}  // namespace acme
