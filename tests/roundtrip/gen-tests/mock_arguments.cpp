// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// What the header's author wrote for controller.h: the definitions the tests link against.

#include "controller.h"

namespace acme {

Controller::Controller(Sensor&, Logger*) {}

Controller::Controller(std::unique_ptr<Sensor>) {}

int Controller::value() const { return 0; }

void Controller::attach(const std::shared_ptr<Logger>&, const Sensor*, int) {}

}  // namespace acme
