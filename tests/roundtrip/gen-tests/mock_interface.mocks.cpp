// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// The generated mocks, compiled: each must be a class that can be built (every
// pure virtual function of its interface declared, with the right specifiers,
// or the mock would still be abstract) and must stand in for the interface.

#include <type_traits>

#include "mock_probe.h"
#include "mock_sensor.h"

namespace {

static_assert(!std::is_abstract<acme::MockSensor>::value, "MockSensor must implement every pure virtual function of Sensor");
static_assert(!std::is_abstract<acme::MockProbe>::value, "MockProbe must implement what Probe leaves pure");
static_assert(std::is_base_of<acme::Sensor, acme::MockSensor>::value, "a mock of Sensor is a Sensor");
static_assert(std::is_base_of<acme::Probe, acme::MockProbe>::value, "a mock of Probe is a Probe");

}  // namespace

int through_the_interface();

// Never called: a mock behind the interface it mocks, with an expectation set and a type with a comma.
int through_the_interface() {
    testing::NiceMock<acme::MockProbe> probe;
    EXPECT_CALL(probe, extra(1, 2)).WillOnce(testing::Return(3.0));
    acme::Sensor& sensor = probe;
    const std::map<int, int> none;
    return static_cast<int>(sensor.table(none).size()) + sensor.last().value;
}
