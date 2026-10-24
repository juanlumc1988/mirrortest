// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "net.h"

namespace acme {
namespace net {

void Socket::open() {}
int Socket::fd() const { return -1; }
unsigned Packet::size() const { return 0; }

}  // namespace net
}  // namespace acme
