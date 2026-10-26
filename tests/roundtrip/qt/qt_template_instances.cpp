// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// A class template has no definitions to link: box.h is all of it. This translation unit only
// checks that the header stands alone, as the library's users would include it.

#include "box.h"

namespace acme {

template class Box<int>;

}  // namespace acme
