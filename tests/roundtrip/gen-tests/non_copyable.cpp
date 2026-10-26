// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The classes of handles.h as a project would have implemented them. IsNotCopyable and
// IsNotMovable are static_asserts: they compile against what the header deletes and, being all
// there is to them, pass; the other tests must stay red.

#include "handles.h"

namespace acme {

Handle::Handle() {}
Handle::Handle(Handle&&) noexcept {}
Handle& Handle::operator=(Handle&&) noexcept { return *this; }
int Handle::fd() const { return -1; }
Pinned::Pinned() {}
int Plain::size() const { return 0; }

}  // namespace acme
