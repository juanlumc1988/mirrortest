// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "sink2.h"

void Queue::push(std::string&&) {}
void Queue::push(const std::string&) {}
