// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "sink.h"

int Sink::write(int fd, Mystery, unsigned) { return fd; }
void Sink::flush() {}
