#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "controller.h"
#include "sensor.h"

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

class MockSensor : public Sensor {
public:
    MOCK_METHOD(int, read, (), (override));
    MOCK_METHOD(void, calibrate, (double offset), (override));
    MOCK_METHOD(std::string, name, (), (const, override));
};

class ControllerTest : public ::testing::Test {
protected:
    NiceMock<MockSensor> sensor;
    Controller controller{&sensor};
};

TEST_F(ControllerTest, ReportsWhatTheSensorReads) {
    EXPECT_CALL(sensor, read()).WillOnce(Return(42));
    EXPECT_EQ(controller.value(), 42);
}

TEST_F(ControllerTest, TakesTheNameFromTheSensor) {
    EXPECT_CALL(sensor, name()).WillOnce(Return("probe"));
    EXPECT_EQ(controller.label(), "probe");
}

TEST(ControllerStrictTest, CalibratesOnce) {
    StrictMock<MockSensor> strict;
    Controller other(&strict);
    EXPECT_CALL(strict, calibrate(0.5));
    other.calibrate(0.5);
}
