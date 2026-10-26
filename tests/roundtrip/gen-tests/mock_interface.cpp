// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// What the header's author wrote for sensor.h: the definitions the tests link
// against. The interfaces have none.

#include "sensor.h"

namespace acme {

int Probe::read() { return 0; }

Controller::Controller(int) {}

int Controller::value() const { return 0; }

}  // namespace acme
