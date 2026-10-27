// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// An implementation of port.h in every configuration of tests/roundtrip/gen-tests/ifdef_branches.configs: what the header
// declares under each, and nothing else, so that a test the generator wrote for a member that is not declared would not link
// and one it wrote without the condition would not compile.

#include "port.h"

void Port::open() {}

#ifdef USE_DMA
void Port::transfer(const char*, unsigned) {}
#elif defined(USE_PIO) && PIO_WIDTH > 8
void Port::transfer(const char*, unsigned, int) {}
#else
void Port::transfer(const char*) {}
#endif

#ifndef NO_STATS
#ifdef VERBOSE
int Port::stats() const { return 0; }
#endif
#endif

#ifdef EXTRA
int Extra::value() const { return 0; }
#endif
